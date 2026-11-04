#include "platform.h"
#include <QKeySequence>
#include <dwmapi.h>
#include <powrprof.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
namespace clip {
QJsonObject processIdentity(quint32 pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    auto close = qScopeGuard([&] { CloseHandle(process); });
    std::wstring path(32768, L'\0');
    DWORD length = DWORD(path.size()), exit = 0;
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length) ||
        !GetProcessTimes(process, &created, &ended, &kernel, &user) || !GetExitCodeProcess(process, &exit) ||
        exit != STILL_ACTIVE)
        return {};
    return {{"pid", qint64(pid)},
            {"executable", QString::fromWCharArray(path.data(), int(length))},
            {"created", QString::number((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime)}};
}
bool processTreeContains(quint32 ancestor, quint32 process) {
    if (!ancestor || !process)
        return false;
    if (ancestor == process)
        return true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot check overlapping application audio process trees");
    auto close = qScopeGuard([&] { CloseHandle(snapshot); });
    QMap<quint32, quint32> parents;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
        do {
            parents[entry.th32ProcessID] = entry.th32ParentProcessID;
        } while (Process32NextW(snapshot, &entry));
    QSet<quint32> visited;
    while (parents.contains(process) && !visited.contains(process)) {
        visited.insert(process);
        auto parent = parents[process];
        if (!parent || parent == process)
            return false;
        const auto childIdentity = processIdentity(process), parentIdentity = processIdentity(parent);
        if (!childIdentity.isEmpty() && !parentIdentity.isEmpty() &&
            parentIdentity["created"].toString().toULongLong() >
                childIdentity["created"].toString().toULongLong())
            return false;
        if (parent == ancestor)
            return true;
        process = parent;
    }
    return false;
}
ApplicationIdentity windowApplication(quintptr value) {
    ApplicationIdentity result;
    HWND window = HWND(value);
    if (!window || !IsWindow(window))
        return result;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId())
        return result;
    const auto identity = processIdentity(pid);
    result.pid = pid;
    result.processCreated = identity["created"].toString();
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        std::wstring path(32768, L'\0');
        DWORD size = DWORD(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
            result.executable = QString::fromWCharArray(path.data(), int(size));
            result.name = QFileInfo(result.executable).completeBaseName();
            result.window = value;
        }
        CloseHandle(process);
    }
    return result;
}
HWND fullscreenCaptureWindow(HMONITOR monitor, HWND windowCapture) {
    const auto window = windowCapture ? windowCapture : GetForegroundWindow();
    if (!window || window == GetShellWindow() || !IsWindowVisible(window) || IsIconic(window) ||
        MonitorFromWindow(window, MONITOR_DEFAULTTONULL) != monitor)
        return nullptr;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    if (IsZoomed(window) && (style & WS_CAPTION) == WS_CAPTION)
        return nullptr;
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) &&
        !GetWindowRect(window, &bounds))
        return nullptr;
    MONITORINFO display{sizeof(display)};
    if (!GetMonitorInfoW(monitor, &display))
        return nullptr;
    const auto &screen = display.rcMonitor;
    return bounds.left <= screen.left && bounds.top <= screen.top &&
                   bounds.right >= screen.right && bounds.bottom >= screen.bottom
               ? window
               : nullptr;
}
QList<QPair<quintptr, QString>> captureWindows() {
    QList<QPair<quintptr, QString>> result;
    EnumWindows(
        [](HWND w, LPARAM p) -> BOOL {
            if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER) || w == GetShellWindow())
                return TRUE;
            DWORD pid;
            GetWindowThreadProcessId(w, &pid);
            if (pid == GetCurrentProcessId())
                return TRUE;
            wchar_t title[1024]{};
            if (GetWindowTextW(w, title, 1024) > 0)
                reinterpret_cast<QList<QPair<quintptr, QString>> *>(p)->append(
                    {quintptr(w), QString::fromWCharArray(title)});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
bool excludeFromCapture(quintptr window) {
    return SetWindowDisplayAffinity(HWND(window), 0x00000011) != FALSE;
}
QJsonObject fileIdentity(const QString &path) {
    HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                              FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    BY_HANDLE_FILE_INFORMATION info{};
    bool ok = GetFileInformationByHandle(file, &info) != FALSE;
    CloseHandle(file);
    if (!ok)
        return {};
    auto number = [](DWORD high, DWORD low) { return QString::number((quint64(high) << 32) | low); };
    return {{"volume", QString::number(info.dwVolumeSerialNumber)},
            {"file", number(info.nFileIndexHigh, info.nFileIndexLow)},
            {"size", number(info.nFileSizeHigh, info.nFileSizeLow)},
            {"created", number(info.ftCreationTime.dwHighDateTime, info.ftCreationTime.dwLowDateTime)},
            {"modified", number(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime)}};
}
bool copyFile(const QString &path, QString *error, int attempts) {
    if (!QFileInfo(path).isFile()) {
        if (error)
            *error = "Saved file no longer exists";
        return false;
    }
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    const SIZE_T bytes = sizeof(DROPFILES) + SIZE_T(native.size() + 2) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!memory) {
        if (error)
            *error = "Cannot allocate file clipboard data";
        return false;
    }
    auto drop = static_cast<DROPFILES *>(GlobalLock(memory));
    if (!drop) {
        GlobalFree(memory);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    memcpy(reinterpret_cast<char *>(drop) + sizeof(DROPFILES), native.utf16(),
           SIZE_T(native.size()) * sizeof(wchar_t));
    GlobalUnlock(memory);
    bool opened = false;
    for (int i = 0; i < attempts; ++i) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30 + i * 20));
    }
    if (!opened) {
        GlobalFree(memory);
        if (error)
            *error = "Clipboard is busy; retry copying the saved file";
        return false;
    }
    bool ok = EmptyClipboard() && SetClipboardData(CF_HDROP, memory) != nullptr;
    if (ok) {
        UINT effect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        auto e = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (e) {
            auto value = static_cast<DWORD *>(GlobalLock(e));
            if (value) {
                *value = DROPEFFECT_COPY;
                GlobalUnlock(e);
                if (!SetClipboardData(effect, e))
                    GlobalFree(e);
            } else
                GlobalFree(e);
        }
    }
    CloseClipboard();
    if (!ok) {
        GlobalFree(memory);
        if (error)
            *error = "Windows rejected the file clipboard data";
    }
    return ok;
}
void showInFolder(const QString &path) {
    PIDLIST_ABSOLUTE item =
        ILCreateFromPathW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
    if (item) {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    } else
        ShellExecuteW(
            nullptr, L"open",
            reinterpret_cast<LPCWSTR>(
                QDir::toNativeSeparators(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath())
                    .utf16()),
            nullptr, nullptr, SW_SHOWNORMAL);
}
bool registerStartup(bool enabled, QString *error) {
    QSettings registry("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                       QSettings::NativeFormat);
    if (enabled)
        registry.setValue("DesktopClipper",
                          '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                              "\" --background");
    else
        registry.remove("DesktopClipper");
    registry.sync();
    if (registry.status() != QSettings::NoError) {
        if (error)
            *error = "Could not update per-user startup registration";
        return false;
    }
    return true;
}
Hotkeys::Hotkeys(QObject *p) : QObject(p) {
    QCoreApplication::instance()->installNativeEventFilter(this);
}
Hotkeys::~Hotkeys() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    for (auto b : bindings_)
        UnregisterHotKey(nullptr, b.id);
}
bool Hotkeys::parse(const QString &text, UINT &modifiers, UINT &key) {
    QKeySequence seq(text, QKeySequence::PortableText);
    if (seq.count() != 1)
        return false;
    auto k = seq[0];
    modifiers = 0;
    if (k.keyboardModifiers() & Qt::ControlModifier)
        modifiers |= MOD_CONTROL;
    if (k.keyboardModifiers() & Qt::ShiftModifier)
        modifiers |= MOD_SHIFT;
    if (k.keyboardModifiers() & Qt::AltModifier)
        modifiers |= MOD_ALT;
    if (k.keyboardModifiers() & Qt::MetaModifier)
        modifiers |= MOD_WIN;
    int value = int(k.key());
    if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
        key = UINT(value);
    else if (value >= Qt::Key_F1 && value <= Qt::Key_F24)
        key = UINT(VK_F1 + value - Qt::Key_F1);
    else if (value == Qt::Key_Space)
        key = VK_SPACE;
    else if (value == Qt::Key_Escape)
        key = VK_ESCAPE;
    else if (value == Qt::Key_Insert)
        key = VK_INSERT;
    else if (value == Qt::Key_Home)
        key = VK_HOME;
    else if (value == Qt::Key_End)
        key = VK_END;
    else if (value == Qt::Key_PageUp)
        key = VK_PRIOR;
    else if (value == Qt::Key_PageDown)
        key = VK_NEXT;
    else
        return false;
    return true;
}
bool Hotkeys::assign(const QString &command, const QString &shortcut, QString *error) {
    if (bindings_.contains(command) && bindings_[command].text == shortcut)
        return true;
    UINT mod = 0, key = 0;
    if (!parse(shortcut, mod, key)) {
        if (error)
            *error = "Choose one supported keyboard shortcut";
        return false;
    }
    int id = nextId_++;
    if (!RegisterHotKey(nullptr, id, mod | MOD_NOREPEAT, key)) {
        if (error)
            *error = "Shortcut is already registered. The existing binding remains active.";
        return false;
    }
    if (bindings_.contains(command))
        UnregisterHotKey(nullptr, bindings_[command].id);
    bindings_[command] = {id, shortcut};
    return true;
}
bool Hotkeys::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
    auto *m = static_cast<MSG *>(message);
    if (m->message == WM_HOTKEY) {
        for (auto i = bindings_.begin(); i != bindings_.end(); ++i)
            if (i->id == int(m->wParam)) {
                emit activated(i.key());
                return true;
            }
    }
    if (m->message == WM_POWERBROADCAST && m->wParam == PBT_APMSUSPEND)
        emit environmentChanged(
            "Windows is suspending; recording stopped safely. Restart capture after resume.");
    if (m->message == WM_DISPLAYCHANGE)
        emit environmentChanged(
            "Display configuration changed; recording stopped. Review capture settings before restarting.");
    return false;
}
} // namespace clip
