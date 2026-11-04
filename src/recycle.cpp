#include "recycle.h"
#include "platform.h"
#include <shlobj.h>
#include <wrl/client.h>
namespace clip {
bool stageForRecycling(const QString &source, const QString &stage, const QJsonObject &expected,
                       QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    auto folder = QFileInfo(stage).dir();
    auto parent = QFileInfo(source).dir().canonicalPath();
    static const QRegularExpression name("^\\.recycle-[0-9a-f-]{36}$");
    if (!name.match(folder.dirName()).hasMatch() ||
        QFileInfo(folder.absolutePath()).dir().canonicalPath() != parent ||
        QFileInfo(stage).fileName() != QFileInfo(source).fileName() || expected.isEmpty())
        return fail("Invalid recycle staging location; original retained");
    if (QFileInfo::exists(stage))
        return fileIdentity(stage) == expected
                   ? true
                   : fail("Recycle staging file was replaced; retained for review");
    if (!QDir().mkpath(folder.absolutePath()) || QFileInfo(folder.absolutePath()).isSymLink() ||
        QFileInfo(folder.absolutePath()).isJunction())
        return fail("Cannot create private recycle staging folder");
    auto nativeFolder = QDir::toNativeSeparators(folder.absolutePath());
    auto folderAttributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeFolder.utf16()));
    if (folderAttributes != INVALID_FILE_ATTRIBUTES)
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeFolder.utf16()),
                           folderAttributes | FILE_ATTRIBUTE_HIDDEN);
    auto native = QDir::toNativeSeparators(source);
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), DELETE | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return fail("Original is open, unavailable or cannot be locked for recycling; retained");
    auto close = qScopeGuard([&] { CloseHandle(file); });
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return fail("Original could not be safely identified; retained");
    auto number = [](DWORD hi, DWORD lo) { return QString::number((quint64(hi) << 32) | lo); };
    QJsonObject identity{
        {"volume", QString::number(info.dwVolumeSerialNumber)},
        {"file", number(info.nFileIndexHigh, info.nFileIndexLow)},
        {"size", number(info.nFileSizeHigh, info.nFileSizeLow)},
        {"created", number(info.ftCreationTime.dwHighDateTime, info.ftCreationTime.dwLowDateTime)},
        {"modified", number(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime)}};
    if (identity != expected)
        return fail("Original changed or was replaced; no file was recycled");
    auto destination = QDir::toNativeSeparators(QFileInfo(stage).absoluteFilePath());
    DWORD bytes = DWORD(destination.size() * sizeof(wchar_t));
    QByteArray data(int(sizeof(FILE_RENAME_INFO) + bytes + sizeof(wchar_t)), 0);
    auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(data.data());
    rename->ReplaceIfExists = FALSE;
    rename->FileNameLength = bytes;
    memcpy(rename->FileName, destination.utf16(), bytes);
    if (!SetFileInformationByHandle(file, FileRenameInfo, rename, DWORD(data.size())))
        return fail("Windows could not stage the locked original; it was retained");
    return true;
}
bool recycleOnly(const QString &path, const QJsonObject &expected, QString *destination, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (expected.isEmpty() || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        fileIdentity(path) != expected)
        return fail("Original is missing, replaced or changed; no file was recycled");
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(init))
            CoUninitialize();
    });
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return fail("Cannot initialize Windows recycling");
    using Microsoft::WRL::ComPtr;
    ComPtr<IShellItem> source, parent, bin, recycled;
    ComPtr<ITransferSource> transfer;
    HRESULT hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr,
                                             IID_PPV_ARGS(&source));
    if (SUCCEEDED(hr))
        hr = source->GetParent(&parent);
    if (SUCCEEDED(hr))
        hr = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(&transfer));
    if (SUCCEEDED(hr))
        hr = SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin));
    if (FAILED(hr))
        return fail(QString("Recycle Bin is unavailable (0x%1); original retained")
                        .arg(quint32(hr), 8, 16, QChar('0')));
    if (fileIdentity(path) != expected)
        return fail("Original changed before recycling; file retained");
    hr = transfer->RecycleItem(source.Get(), bin.Get(), TSF_NORMAL, &recycled);
    if (FAILED(hr) || !recycled)
        return fail(
            QString("Windows could not recycle the original (0x%1). No permanent deletion was attempted.")
                .arg(quint32(hr), 8, 16, QChar('0')));
    PWSTR name = nullptr;
    if (SUCCEEDED(recycled->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name))) {
        if (destination)
            *destination = QString::fromWCharArray(name);
        CoTaskMemFree(name);
    }
    if (QFileInfo::exists(path))
        return fail("Windows did not remove the original from its source location");
    return true;
}
} // namespace clip
