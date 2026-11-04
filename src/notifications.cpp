#include "notifications.h"
#include <QPointer>
#include <QUrlQuery>
#include <notificationactivationcallback.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <wrl.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
namespace clip {
NotificationAction parseNotificationAction(const QString &text) {
    NotificationAction result;
    if (text.size() > 256)
        return result;
    QUrlQuery query(text);
    const auto items = query.queryItems();
    if (items.size() != 2 || items[0].first != "job" || items[1].first != "action")
        return result;
    static const QRegularExpression uuid("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    result.job = items[0].second;
    result.action = items[1].second;
    result.valid = uuid.match(result.job).hasMatch() &&
                   QStringList{"open", "open-original", "yes", "no", "retry"}.contains(result.action);
    return result;
}
namespace {
QString mediaSize(const QJsonObject &media) {
    return QString::number(double(media["bytes"].toInteger()) / 1000000., 'f', 2) + " MB";
}
QString mediaDetails(const QJsonObject &media) {
    if (media["bytes"].toInteger() <= 0)
        return {};
    return QString("%1×%2 · %3 fps · %4 s")
        .arg(media["width"].toInt())
        .arg(media["height"].toInt())
        .arg(media["fps"].toDouble(), 0, 'g', 4)
        .arg(media["duration"].toDouble(), 0, 'f', 1);
}
QString toastVisual(const QString &arguments, const QString &title, const QString &body) {
    return "<toast launch=\"" + arguments.toHtmlEscaped() +
           "\"><visual><binding template=\"ToastGeneric\"><text>" + title.toHtmlEscaped() +
           "</text><text>" + body.toHtmlEscaped() + "</text></binding></visual>";
}
}
QString savedNotificationXml(const QString &id, const QJsonObject &media) {
    const QString args = "job=" + id + "&action=open-original";
    return parseNotificationAction(args).valid
        ? toastVisual(args, "Clip Saved", mediaSize(media) + " · " + mediaDetails(media)) +
            "<audio silent=\"true\"/></toast>"
        : QString{};
}
QString notificationXml(const ExportJob &job) {
    QString arguments = "job=" + job.id + "&action=";
    if (!parseNotificationAction(arguments + "open").valid || !job.terminal)
        return {};
    bool compressed = job.kind == "compress" || job.kind == "save-compress";
    bool question = compressed && job.state == "Complete" && job.details["copied"].toBool() &&
                    !job.details["alreadySmall"].toBool() &&
                    job.details["deleteDecision"].toString() == "Pending";
    const auto media = job.details["outputMedia"].toObject();
    const auto details = mediaDetails(media);
    QString title = "Auto Compress";
    QString text;
    if (question) {
        title = details.isEmpty() ? "Clip Compressed" : "Compressed to " + mediaSize(media);
        text = "Copied to clipboard. Delete original?";
        if (!details.isEmpty())
            text += "\n" + details;
    } else if (job.state == "Cannot meet constraints")
        text = "File was too big to reduce to 20MB.";
    else if (job.state == "Compressed but clipboard copy failed")
        text = "Compressed, but could not copy.";
    else if (job.state == "Already within limit") {
        title = "Clip Ready to Share";
        text = "Already within 20 MB and copied to clipboard.";
        if (!details.isEmpty())
            text += "\n" + mediaSize(media) + " · " + details;
    } else if (job.state == "Complete") {
        title = job.kind == "edit" ? "Edited Clip Saved" : "Clip Saved";
        text = job.message;
    } else
        text = job.state + ". " + job.message;
    QString xml = toastVisual(arguments + "open", title, text);
    if (question || job.details["copyPending"].toBool()) {
        xml += "<actions>";
        auto action = [&](const QString &label, const QString &command) {
            xml += "<action content=\"" + label + "\" activationType=\"foreground\" arguments=\"" +
                   (arguments + command).toHtmlEscaped() + "\"/>";
        };
        if (question) {
            action("Yes", "yes");
            action("No", "no");
        } else
            action("Retry Copy", "retry");
        xml += "</actions>";
    }
    return xml + "<audio silent=\"true\"/></toast>";
}
namespace {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
constexpr auto Classic = Microsoft::WRL::ClassicCom;
class Activator final : public RuntimeClass<RuntimeClassFlags<Classic>, INotificationActivationCallback> {
    std::function<void(QString)> callback_;

  public:
    explicit Activator(std::function<void(QString)> callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR appId, LPCWSTR args, const NOTIFICATION_USER_INPUT_DATA *,
                                       ULONG) override {
        if (!appId || wcscmp(appId, Notifications::AppId) != 0 || !args)
            return E_INVALIDARG;
        try {
            if (wcsnlen_s(args, 257) > 256)
                return E_INVALIDARG;
            auto text = QString::fromWCharArray(args);
            if (!parseNotificationAction(text).valid)
                return E_INVALIDARG;
            callback_(text);
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }
};
class Factory final : public RuntimeClass<RuntimeClassFlags<Classic>, IClassFactory> {
    std::function<void(QString)> callback_;

  public:
    explicit Factory(std::function<void(QString)> callback) : callback_(std::move(callback)) {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto instance = Make<Activator>(callback_);
        return instance ? instance.CopyTo(iid, object) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override {
        return S_OK;
    }
};
CLSID activatorClass() {
    CLSID id{};
    winrt::check_hresult(CLSIDFromString(Notifications::ClassId, &id));
    return id;
}
QString shellLinkPath() {
    PWSTR programs = nullptr;
    winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs));
    const auto freePath = qScopeGuard([&] { CoTaskMemFree(programs); });
    return QDir(QString::fromWCharArray(programs)).filePath(QString(AppName) + ".lnk");
}
QString shortcutTarget(const QString &path) {
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return {};
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file)) || FAILED(file->Load(path.toStdWString().c_str(), STGM_READ)))
        return {};
    ComPtr<IPropertyStore> properties;
    PROPVARIANT value{};
    const auto clear = qScopeGuard([&] { PropVariantClear(&value); });
    if (FAILED(link.As(&properties)) || FAILED(properties->GetValue(PKEY_AppUserModel_ID, &value)) ||
        value.vt != VT_LPWSTR || !value.pwszVal || wcscmp(value.pwszVal, Notifications::AppId) != 0)
        return {};
    wchar_t target[32768]{};
    if (FAILED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)))
        return {};
    return QFileInfo(QString::fromWCharArray(target)).absoluteFilePath();
}
} // namespace
struct Notifications::Impl {
    DWORD cookie = 0;
    bool initialized = false;
    ComPtr<IClassFactory> factory;
    ~Impl() {
        if (cookie)
            CoRevokeClassObject(cookie);
        factory.Reset();
        if (initialized)
            CoUninitialize();
    }
};
Notifications::Notifications(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
    auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl_->initialized = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        winrt::check_hresult(hr);
    QPointer<Notifications> guard(this);
    impl_->factory = Make<Factory>([guard](QString args) {
        if (guard)
            QMetaObject::invokeMethod(
                guard,
                [guard, args] {
                    if (guard)
                        emit guard->activated(args);
                },
                Qt::QueuedConnection);
    });
    winrt::check_hresult(CoRegisterClassObject(activatorClass(), impl_->factory.Get(), CLSCTX_LOCAL_SERVER,
                                               REGCLS_MULTIPLEUSE, &impl_->cookie));
    winrt::check_hresult(SetCurrentProcessExplicitAppUserModelID(AppId));
}
Notifications::~Notifications() = default;
namespace {
bool submitToast(const QString &xml, const QString &id, const wchar_t *group, QString *error, bool suppressPopup) {
    QString operation = "Constructing notification";
    try {
        if (xml.isEmpty())
            throw std::runtime_error("Job has no completed notification");
        using namespace winrt::Windows::UI::Notifications;
        operation = "Creating Windows toast notifier";
        auto notifier = ToastNotificationManager::CreateToastNotifier(Notifications::AppId);
        operation = "Reading Windows notification settings";
        try {
            if (notifier.Setting() != NotificationSetting::Enabled) {
                if (error)
                    *error =
                        "Windows notifications are disabled or suppressed; actions remain in Export jobs";
                return false;
            }
        } catch (const winrt::hresult_error &e) {
            // Windows 10 can have no settings record until this AUMID submits its first toast.
            // Show still obeys Windows notification policy; other settings failures remain errors.
            if (e.code().value != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
                throw;
        }
        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        operation = "Parsing notification XML";
        document.LoadXml(xml.toStdWString());
        ToastNotification toast(document);
        toast.SuppressPopup(suppressPopup);
        toast.Tag(id.left(16).toStdWString());
        toast.Group(group);
        operation = "Submitting Windows notification";
        notifier.Show(toast);
        return true;
    } catch (const winrt::hresult_error &e) {
        if (error)
            *error = operation + QString(" (0x%1): ").arg(quint32(e.code().value), 8, 16, QChar('0')) +
                     QString::fromWCharArray(e.message().c_str());
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
}
bool Notifications::show(const ExportJob &job, QString *error, bool suppressPopup) {
    return submitToast(notificationXml(job), job.id, L"exports", error, suppressPopup);
}
bool Notifications::showSaved(const QString &id, const QJsonObject &media, QString *error, bool suppressPopup) {
    return submitToast(savedNotificationXml(id, media), id, L"saves", error, suppressPopup);
}
bool Notifications::registerShell(bool install, QString *error) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto uninit = qScopeGuard([&] {
        if (SUCCEEDED(init))
            CoUninitialize();
    });
    try {
        if (FAILED(init) && init != RPC_E_CHANGED_MODE)
            winrt::check_hresult(init);
        QString exe = QCoreApplication::applicationFilePath();
        QString shortcut = shellLinkPath();
        QString key = "HKEY_CURRENT_USER\\Software\\Classes\\CLSID\\" + QString::fromWCharArray(ClassId) +
                      "\\LocalServer32";
        QSettings registration(key, QSettings::NativeFormat);
        QSettings assets("HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId\\" +
                             QString::fromWCharArray(AppId),
                         QSettings::NativeFormat);
        if (!install) {
            if (shortcutTarget(shortcut).compare(QFileInfo(exe).absoluteFilePath(), Qt::CaseInsensitive) == 0)
                if (!QFile::remove(shortcut))
                    throw std::runtime_error("Cannot remove notification shortcut");
            QString expected = '"' + QDir::toNativeSeparators(exe) + "\" --notification-server";
            if (registration.value(".").toString() == expected) {
                registration.remove("");
                registration.sync();
            }
            if (assets.value("OwnerExecutable").toString().compare(exe, Qt::CaseInsensitive) == 0) {
                assets.remove("");
                assets.sync();
            }
            return registration.status() == QSettings::NoError;
        }
        const auto target = shortcutTarget(shortcut);
        if (QFileInfo::exists(shortcut) && target.isEmpty())
            throw std::runtime_error("A different SippingCloftware shortcut already exists");
        const QString command = '"' + QDir::toNativeSeparators(exe) + "\" --notification-server";
        if (target.compare(QFileInfo(exe).absoluteFilePath(), Qt::CaseInsensitive) == 0 &&
            registration.value(".").toString() == command && assets.value("OwnerExecutable") == exe)
            return true;
        if (!QDir().mkpath(QFileInfo(shortcut).absolutePath()))
            throw std::runtime_error("Cannot create Start Menu folder");
        ComPtr<IShellLinkW> link;
        winrt::check_hresult(
            CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
        winrt::check_hresult(link->SetPath(exe.toStdWString().c_str()));
        winrt::check_hresult(link->SetWorkingDirectory(QFileInfo(exe).absolutePath().toStdWString().c_str()));
        winrt::check_hresult(link->SetDescription(L"Record, edit and share local clips"));
        ComPtr<IPropertyStore> properties;
        winrt::check_hresult(link.As(&properties));
        PROPVARIANT value{};
        winrt::check_hresult(InitPropVariantFromString(AppId, &value));
        auto clear = qScopeGuard([&] { PropVariantClear(&value); });
        winrt::check_hresult(properties->SetValue(PKEY_AppUserModel_ID, value));
        PropVariantClear(&value);
        auto id = activatorClass();
        winrt::check_hresult(InitPropVariantFromCLSID(id, &value));
        winrt::check_hresult(properties->SetValue(PKEY_AppUserModel_ToastActivatorCLSID, value));
        winrt::check_hresult(properties->Commit());
        ComPtr<IPersistFile> file;
        winrt::check_hresult(link.As(&file));
        winrt::check_hresult(file->Save(shortcut.toStdWString().c_str(), TRUE));
        registration.setValue(".", command);
        registration.sync();
        assets.setValue("DisplayName", QString(AppName));
        assets.setValue("CustomActivator", QString::fromWCharArray(ClassId));
        assets.setValue("OwnerExecutable", exe);
        assets.sync();
        if (registration.status() != QSettings::NoError || assets.status() != QSettings::NoError)
            throw std::runtime_error("Cannot register per-user notification activation");
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return true;
    } catch (const winrt::hresult_error &e) {
        if (error)
            *error = QString::fromWCharArray(e.message().c_str());
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}
} // namespace clip
