#include "notifications.h"
#include "instance.h"
#include "recycle.h"
#include "platform.h"
#include <QtTest>
#include <notificationactivationcallback.h>
#include <wrl/client.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
class NotificationTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    clip::ExportJob completedJob(const QString &name) {
        clip::ExportJob job;
        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.kind = "compress";
        job.state = "Complete";
        job.terminal = true;
        job.original = temporary_.filePath(name + "-original.txt");
        job.output = temporary_.filePath(name + "-compressed.txt");
        QFile a(job.original);
        if (!a.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot create original fixture");
        a.write("test original retained until explicit Yes");
        a.close();
        QFile b(job.output);
        if (!b.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot create compressed fixture");
        b.write("test compressed copy");
        b.close();
        job.details = {{"copied", true},
                       {"deleteDecision", "Pending"},
                       {"originalIdentity", clip::fileIdentity(job.original)},
                       {"outputIdentity", clip::fileIdentity(job.output)}};
        return job;
    }
  private slots:
    void initTestCase() {
        qputenv("CLIPPER_STATE_DIR", temporary_.path().toUtf8());
    }
    void argumentsCannotNameFiles() {
        QString id = "b17706db-c2ec-4c97-9013-0960fa96065e";
        QVERIFY(clip::parseNotificationAction("job=" + id + "&action=yes").valid);
        for (auto value : QStringList{"job=C:/clip.mp4&action=yes", "job=" + id + "&action=delete",
                                      "job=" + id + "&action=yes&path=C:/clip.mp4",
                                      "job=" + id + "&job=" + id, "job=" + id + "&action=yes&action=no"})
            QVERIFY(!clip::parseNotificationAction(value).valid);
    }
    void exactMessagesAndActions() {
        clip::ExportJob job;
        job.id = "b17706db-c2ec-4c97-9013-0960fa96065e";
        job.terminal = true;
        job.state = "Complete";
        job.kind = "compress";
        job.details = {{"copied", true}, {"deleteDecision", "Pending"}};
        auto xml = clip::notificationXml(job);
        QVERIFY(xml.contains("File compressed and copied to clipboard. Delete unreduced file?"));
        QVERIFY(xml.contains("content=\"Yes\""));
        QVERIFY(xml.contains("content=\"No\""));
        QVERIFY(xml.contains("&amp;action=yes"));
        job.state = "Already within limit";
        job.details["alreadySmall"] = true;
        QVERIFY(!clip::notificationXml(job).contains("<actions>"));
        job.state = "Cannot meet constraints";
        QVERIFY(clip::notificationXml(job).contains("File was too big to reduce to 20MB."));
        job.state = "Compressed but clipboard copy failed";
        job.details["copyPending"] = true;
        xml = clip::notificationXml(job);
        QVERIFY(xml.contains("Compressed, but could not copy."));
        QVERIFY(xml.contains("Retry Copy"));
        QVERIFY(!xml.contains("Delete unreduced"));
    }
    void actualComActivation() {
        clip::Notifications notifications;
        QSignalSpy spy(&notifications, &clip::Notifications::activated);
        CLSID clsid{};
        QVERIFY(SUCCEEDED(CLSIDFromString(clip::Notifications::ClassId, &clsid)));
        Microsoft::WRL::ComPtr<INotificationActivationCallback> callback;
        HRESULT result = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&callback));
        QVERIFY2(SUCCEEDED(result), qPrintable(QString::number(quint32(result), 16)));
        QVERIFY(SUCCEEDED(callback->Activate(clip::Notifications::AppId,
                                             L"job=b17706db-c2ec-4c97-9013-0960fa96065e&action=open", nullptr,
                                             0)));
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().toString(),
                 QString("job=b17706db-c2ec-4c97-9013-0960fa96065e&action=open"));
        QVERIFY(FAILED(
            callback->Activate(clip::Notifications::AppId, L"path=C:/other.mp4&action=yes", nullptr, 0)));
    }
    void recycleActualOwnedFixture() {
        auto original = temporary_.filePath("recycle-fixture.txt");
        QFile f(original);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("reversible test fixture");
        f.close();
        auto identity = clip::fileIdentity(original);
        auto stage = temporary_.filePath(".recycle-b17706db-c2ec-4c97-9013-0960fa96065e/recycle-fixture.txt");
        QString error, destination;
        QVERIFY2(clip::stageForRecycling(original, stage, identity, &error), qPrintable(error));
        QCOMPARE(clip::fileIdentity(stage), identity);
        QVERIFY(!QFileInfo::exists(original));
        QVERIFY2(clip::recycleOnly(stage, identity, &destination, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(stage));
        QVERIFY(!destination.isEmpty());
    }
    void recycleRejectsReplacement() {
        auto original = temporary_.filePath("replacement.txt");
        QFile f(original);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original");
        f.close();
        auto identity = clip::fileIdentity(original);
        QVERIFY(QFile::remove(original));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("keep this replacement");
        f.close();
        QString error;
        QVERIFY(!clip::recycleOnly(original, identity, nullptr, &error));
        QVERIFY(QFileInfo::exists(original));
    }
    void persistedNoWinsOverRepeatedYes() {
        auto job = completedJob("no");
        auto folder = temporary_.filePath("no-jobs");
        QVERIFY(clip::writeJson(QDir(folder).filePath(job.id + ".json"), job.json()));
        clip::ReplayBuffer buffer;
        {
            clip::ExportQueue queue(&buffer, nullptr, folder);
            QVERIFY(queue.decideOriginal(job.id, false));
            QVERIFY(queue.decideOriginal(job.id, true));
            QCOMPARE(queue.jobs().first().details["deleteDecision"].toString(), QString("Retained"));
        }
        clip::ExportQueue restarted(&buffer, nullptr, folder);
        QVERIFY(restarted.decideOriginal(job.id, true));
        QCOMPARE(clip::fileIdentity(job.original), job.details["originalIdentity"].toObject());
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
    }
    void persistedYesRecyclesOnlyOriginal() {
        auto job = completedJob("yes");
        auto folder = temporary_.filePath("yes-jobs");
        QVERIFY(clip::writeJson(QDir(folder).filePath(job.id + ".json"), job.json()));
        clip::ReplayBuffer buffer;
        {
            clip::ExportQueue queue(&buffer, nullptr, folder);
            QString error;
            QVERIFY2(queue.decideOriginal(job.id, true, &error), qPrintable(error));
            QVERIFY(!QFileInfo::exists(job.original));
            QCOMPARE(queue.jobs().first().details["deleteDecision"].toString(), QString("Recycled"));
            QVERIFY(!queue.jobs().first().details["recycleDestination"].toString().isEmpty());
        }
        clip::ExportQueue restarted(&buffer, nullptr, folder);
        QVERIFY(restarted.decideOriginal(job.id, true));
        QVERIFY(restarted.decideOriginal(job.id, false));
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
    }
    void interruptedRecyclingCanRetainOriginal() {
        auto job = completedJob("interrupted");
        auto stage = QDir(QFileInfo(job.original).absolutePath())
                         .filePath(".recycle-" + job.id + '/' + QFileInfo(job.original).fileName());
        QString error;
        QVERIFY2(
            clip::stageForRecycling(job.original, stage, job.details["originalIdentity"].toObject(), &error),
            qPrintable(error));
        job.details["recycleStage"] = stage;
        job.details["deleteDecision"] = "Recycling";
        auto folder = temporary_.filePath("interrupted-jobs");
        QVERIFY(clip::writeJson(QDir(folder).filePath(job.id + ".json"), job.json()));
        clip::ReplayBuffer buffer;
        clip::ExportQueue restarted(&buffer, nullptr, folder);
        QCOMPARE(restarted.jobs().first().details["deleteDecision"].toString(),
                 QString("Recycle interrupted"));
        QVERIFY(restarted.decideOriginal(job.id, false));
        QCOMPARE(clip::fileIdentity(job.original), job.details["originalIdentity"].toObject());
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
    }
    void stalePersistedYesRetainsReplacement() {
        auto job = completedJob("stale");
        auto folder = temporary_.filePath("stale-jobs");
        QVERIFY(clip::writeJson(QDir(folder).filePath(job.id + ".json"), job.json()));
        QFile f(job.original);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("different file content");
        f.close();
        auto replacement = clip::fileIdentity(job.original);
        clip::ReplayBuffer buffer;
        clip::ExportQueue queue(&buffer, nullptr, folder);
        QString error;
        QVERIFY(!queue.decideOriginal(job.id, true, &error));
        QCOMPARE(clip::fileIdentity(job.original), replacement);
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
    }
    void lockedOriginalFailureRetainsBoth() {
        auto job = completedJob("locked");
        auto folder = temporary_.filePath("locked-jobs");
        QVERIFY(clip::writeJson(QDir(folder).filePath(job.id + ".json"), job.json()));
        HANDLE file = CreateFileW(job.original.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        QVERIFY(file != INVALID_HANDLE_VALUE);
        auto close = qScopeGuard([&] { CloseHandle(file); });
        clip::ReplayBuffer buffer;
        clip::ExportQueue queue(&buffer, nullptr, folder);
        QString error;
        QVERIFY(!queue.decideOriginal(job.id, true, &error));
        QCOMPARE(queue.jobs().first().details["deleteDecision"].toString(), QString("Recycle failed"));
        QCOMPARE(clip::fileIdentity(job.original), job.details["originalIdentity"].toObject());
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
    }
    void nativeToastHistory() {
        QSettings policy("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications",
                         QSettings::NativeFormat);
        if (policy.value("ToastEnabled", 1).toInt() == 0)
            QSKIP("Windows global notifications are disabled by the user's settings. Native popup/history "
                  "delivery remains unverified; do not change system preferences for this test.");
        QString exe = QCoreApplication::applicationDirPath() + "/clipper.exe";
        auto registration = clip::runProcess(
            exe, {"--register-shell", "--result", temporary_.filePath("toast-shell.json")}, 10000);
        QVERIFY2(registration.code == 0,
                 qPrintable(clip::readJson(temporary_.filePath("toast-shell.json"))["error"].toString()));
        auto unregister = qScopeGuard([&] { clip::runProcess(exe, {"--unregister-shell"}, 10000); });
        auto job = completedJob("native-toast");
        clip::Notifications notifications;
        using namespace winrt::Windows::UI::Notifications;
        auto history = ToastNotificationManager::History();
        auto cleanup = qScopeGuard([&] {
            try {
                history.Remove(job.id.left(16).toStdWString(), L"exports", clip::Notifications::AppId);
            } catch (...) {
            }
        });
        QString error;
        QVERIFY2(notifications.show(job, &error, true), qPrintable(error));
        auto present = [&] {
            for (const auto &toast : history.GetHistory(clip::Notifications::AppId))
                if (QString::fromWCharArray(toast.Tag().c_str()) == job.id.left(16)) {
                    auto xml = QString::fromWCharArray(toast.Content().GetXml().c_str());
                    return xml.contains("File compressed and copied to clipboard. Delete unreduced file?") &&
                           xml.contains("content=\"Yes\"") && xml.contains("content=\"No\"");
                }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(present(), 5000);
    }
    void externalRestartActivation() {
        auto job = completedJob("restart-activation");
        auto jobPath = temporary_.filePath("jobs/" + job.id + ".json");
        QVERIFY(clip::writeJson(jobPath, job.json()));
        clip::Settings settings;
        settings.library = temporary_.filePath("library");
        settings.bufferFolder = temporary_.filePath("buffer");
        settings.overlayHotkey = "Ctrl+Alt+Shift+F23";
        settings.recordHotkey = "Ctrl+Alt+Shift+F24";
        QVERIFY(settings.save());
        QString exe = QCoreApplication::applicationDirPath() + "/clipper.exe";
        auto registration =
            clip::runProcess(exe, {"--register-shell", "--result", temporary_.filePath("shell.json")}, 10000);
        QVERIFY2(registration.code == 0,
                 qPrintable(clip::readJson(temporary_.filePath("shell.json"))["error"].toString()));
        auto cleanup = qScopeGuard([&] {
            clip::runProcess(exe, {"--exit"}, 10000);
            clip::runProcess(exe, {"--unregister-shell"}, 10000);
        });
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY(SUCCEEDED(init));
        auto uninit = qScopeGuard([&] { CoUninitialize(); });
        CLSID clsid{};
        QVERIFY(SUCCEEDED(CLSIDFromString(clip::Notifications::ClassId, &clsid)));
        Microsoft::WRL::ComPtr<INotificationActivationCallback> callback;
        auto result = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&callback));
        QVERIFY2(SUCCEEDED(result), qPrintable(QString::number(quint32(result), 16)));
        auto arguments = ("job=" + job.id + "&action=no").toStdWString();
        QVERIFY(SUCCEEDED(callback->Activate(clip::Notifications::AppId, arguments.c_str(), nullptr, 0)));
        QTRY_COMPARE_WITH_TIMEOUT(clip::readJson(jobPath)["details"].toObject()["deleteDecision"].toString(),
                                  QString("Retained"), 10000);
        QCOMPARE(clip::fileIdentity(job.original), job.details["originalIdentity"].toObject());
        QCOMPARE(clip::fileIdentity(job.output), job.details["outputIdentity"].toObject());
        qint64 pid = 0;
        QString host, application;
        QLockFile lock(temporary_.filePath("application.lock"));
        QVERIFY(lock.getLockInfo(&pid, &host, &application));
        struct Windows {
            DWORD pid;
            bool visible = false;
        } windows{DWORD(pid)};
        EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                auto *state = reinterpret_cast<Windows *>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (pid == state->pid && IsWindowVisible(window))
                    state->visible = true;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&windows));
        QVERIFY(!windows.visible);
    }
    void singleInstanceDelivery() {
        clip::Instance owner;
        QString error;
        QVERIFY2(owner.acquire(&error), qPrintable(error));
        QSignalSpy spy(&owner, &clip::Instance::received);
        clip::Instance secondary;
        QVERIFY(!secondary.acquire());
        std::atomic_bool done = false, passed = false;
        std::thread sender([&] {
            passed = secondary.forward({{"command", "show"}});
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 10000);
        sender.join();
        QVERIFY(passed);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().toJsonObject()["command"].toString(), QString("show"));
    }
};
QTEST_GUILESS_MAIN(NotificationTests)
#include "notification_tests.moc"
