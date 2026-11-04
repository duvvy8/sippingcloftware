#include "recorder.h"
#include "registry.h"
#include "ui.h"
#include <QApplication>
#include <QFontDatabase>
#include <QMainWindow>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("DesktopClipper");
    QCoreApplication::setOrganizationName("Local");
    av_log_set_level(AV_LOG_ERROR);
    const QStringList args = app.arguments();
    auto value = [&](const QString &k) {
        int i = args.indexOf(k);
        return i >= 0 ? args.value(i + 1) : QString();
    };
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
        clip::applyAppearance(app);
        app.setQuitOnLastWindowClosed(false);
        clip::MainWindow window;
        if (!args.contains("--background"))
            window.show();
        return app.exec();
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "Startup failed", QString::fromUtf8(e.what()));
        return 1;
    }
}
