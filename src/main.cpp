#include "recorder.h"
#include "registry.h"
#include "ui.h"
#include "notifications.h"
#include "instance.h"
#include <winrt/base.h>
#include <QApplication>
#include <QFontDatabase>
#include <QMainWindow>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(clip::AppName);
    QCoreApplication::setOrganizationName("Local");
    av_log_set_level(AV_LOG_ERROR);
    const QStringList args = app.arguments();
    auto value = [&](const QString &k) {
        int i = args.indexOf(k);
        return i >= 0 ? args.value(i + 1) : QString();
    };
    if (args.contains("--status") || args.contains("--save-clip")) {
        const auto destination = value("--result");
        if (!QFileInfo(destination).isAbsolute())
            return 2;
        const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip::Instance client;
        QString error;
        // A read/control client must never acquire the primary-instance lock:
        // doing so can make a concurrently starting application exit as secondary.
        QJsonObject message{{"command", args.contains("--save-clip") ? "save-clip" : "status"},
                            {"result", destination}, {"request", request}};
        if (args.contains("--save-clip")) {
            bool valid = false;
            const double seconds = value("--save-clip").toDouble(&valid);
            if (!valid || seconds <= 0 || seconds > 86400) {
                clip::writeJson(destination, {{"error", "Provide a clip duration in seconds, from 0 to 86400"}});
                return 2;
            }
            message["seconds"] = seconds;
        }
        if (!client.forward(message, &error, 30000)) {
            clip::writeJson(destination, {{"error", error}});
            return 1;
        }
        const auto report = clip::readJson(destination);
        if (report["request"].toString() != request) {
            clip::writeJson(destination, {{"error", "The running application does not support this request"}});
            return 1;
        }
        return report.contains("error") ? 1 : 0;
    }
    if (args.contains("--probe")) {
        auto result = clip::Registry::testEncoder(value("--probe"), args.contains("--hdr"));
        clip::writeJson(value("--result"), result);
        return result["encoded"].toBool() ? 0 : 1;
    }
    if (args.contains("--diagnostics")) {
        clip::Registry r;
        r.discover();
        if (args.contains("--test-encoders"))
            r.probe({}, nullptr, !args.contains("--no-cache"));
        return clip::writeJson(value("--diagnostics"), r.json()) ? 0 : 1;
    }
    if (args.contains("--record-smoke")) {
        QJsonObject report;
        try {
            clip::Registry r;
            r.discover();
            r.probe();
            clip::Settings s;
            s.encoder = value("--encoder");
            if (s.encoder.isEmpty())
                s.encoder = "h264_nvenc";
            s.fps = 60;
            s.height = 720;
            s.backend = args.contains("--dxgi") ? "dxgi" : "wgc";
            s.borderlessDisplay = args.contains("--dxgi");
            QTemporaryDir buffer;
            s.bufferFolder = buffer.path();
            s.retention = 8;
            if (args.contains("--system-audio")) {
                for (auto d : clip::AudioEngine::devices())
                    if (!d.input && d.defaultDevice) {
                        clip::AudioSource a;
                        a.id = d.id;
                        a.name = d.name;
                        a.separate = true;
                        s.audio << a;
                        break;
                    }
            }
            if (args.contains("--fixture")) {
                HWND window = FindWindowW(nullptr, L"Capture verification");
                if (!window)
                    throw std::runtime_error("Capture fixture is not running");
                s.source = "window";
                s.window = quintptr(window);
            }
            clip::Recorder recorder;
            recorder.start(s, r);
            int seconds = value("--seconds").toInt();
            if (seconds < 3)
                seconds = 6;
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            double endpoint = clip::clockSeconds();
            auto pin = recorder.buffer().pin(endpoint, 3.);
            int delay = args.contains("--post-delay") ? value("--post-delay").toInt() : 700;
            std::this_thread::sleep_for(std::chrono::milliseconds(qMax(0, delay)));
            recorder.stop();
            auto resolved = recorder.buffer().resolve(pin.id);
            auto output = QFileInfo(value("--record-smoke")).absolutePath() + "/capture-" +
                          QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp4";
            clip::saveInterval(resolved, s, output);
            report = {{"stats", recorder.stats().json()},
                      {"clip", clip::inspect(output).json()},
                      {"output", output},
                      {"endpoint", endpoint},
                      {"selectedEnd", resolved.end},
                      {"duration", resolved.duration()},
                      {"segments", resolved.segments.size()}};
            recorder.buffer().release(pin.id);
            recorder.buffer().clear();
        } catch (const std::exception &e) {
            report["error"] = QString::fromUtf8(e.what());
        }
        clip::writeJson(value("--record-smoke"), report);
        return report.contains("error") ? 1 : 0;
    }
    try {
        if (args.contains("--register-shell") || args.contains("--unregister-shell")) {
            QString error;
            bool ok = clip::Notifications::registerShell(args.contains("--register-shell"), &error);
            if (!value("--result").isEmpty())
                clip::writeJson(value("--result"), {{"ok", ok}, {"error", error}});
            return ok ? 0 : 1;
        }
        clip::applyAppearance(app);
        app.setQuitOnLastWindowClosed(false);
        const bool activation = args.contains("--notification-server");
        const bool showControls = args.contains("--show") || !QSystemTrayIcon::isSystemTrayAvailable();
        clip::Instance instance;
        QString error;
        bool primary = instance.acquire(&error);
        if (args.contains("--exit"))
            return primary ? 0 : instance.forward({{"command", "exit"}}, &error) ? 0 : 1;
        if (!primary && !activation)
            return showControls && !instance.forward({{"command", "show"}}, &error) ? 1 : 0;
        if (primary && !activation && !qEnvironmentVariableIsSet("CLIPPER_STATE_DIR") &&
            !clip::Notifications::registerShell(true, &error))
            qWarning("Windows notification setup: %s", qPrintable(error));
        clip::Notifications notifications;
        if (!primary) {
            QObject::connect(&notifications, &clip::Notifications::activated, &app, [&](QString arguments) {
                bool delivered =
                    instance.forward({{"command", "notification"}, {"arguments", arguments}}, &error);
                QCoreApplication::exit(delivered ? 0 : 1);
            });
            QTimer::singleShot(30000, &app, [] { QCoreApplication::exit(1); });
            return app.exec();
        }
        clip::MainWindow window(activation);
        QObject::connect(&notifications, &clip::Notifications::activated, &window,
                         &clip::MainWindow::handleNotificationAction);
        QObject::connect(&instance, &clip::Instance::received, &window, [&](QJsonObject message) {
            if (message["command"].toString() == "notification")
                window.handleNotificationAction(message["arguments"].toString());
            else if (message["command"].toString() == "exit")
                window.exitApplication();
            else if (message["command"].toString() == "show")
                window.showControls();
            else if (message["command"] == "status" || message["command"] == "save-clip") {
                const auto destination = message["result"].toString();
                if (!QFileInfo(destination).isAbsolute())
                    return;
                QJsonObject report;
                try {
                    if (message["command"] == "save-clip")
                        report["job"] = window.saveRecent(message["seconds"].toDouble());
                    report["status"] = window.runtimeStatus();
                } catch (const std::exception &e) {
                    report["error"] = QString::fromUtf8(e.what());
                }
                report["request"] = message["request"];
                clip::writeJson(destination, report);
            }
        });
        QObject::connect(&window, &clip::MainWindow::clipSaved, &window, [&](const QString &id, const QJsonObject &media) {
            QString reason;
            if (window.notificationsEnabled() && !notifications.showSaved(id, media, &reason))
                qWarning("Native notification: %s", qPrintable(reason));
        });
        QObject::connect(&window, &clip::MainWindow::notificationReady, &window, [&](QString id) {
            if (!window.notificationsEnabled())
                return;
            for (const auto &job : window.exportJobs())
                if (job.id == id) {
                    QString reason;
                    if (!notifications.show(job, &reason))
                        qWarning("Native notification: %s", qPrintable(reason));
                }
        });
        if (!activation && showControls)
            window.showControls();
        return app.exec();
    } catch (const winrt::hresult_error &e) {
        QMessageBox::critical(nullptr, "Startup failed", QString::fromWCharArray(e.message().c_str()));
        return 1;
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "Startup failed", QString::fromUtf8(e.what()));
        return 1;
    }
}
