#include <QtWidgets>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
struct WaveTone {
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    ~WaveTone() {
        if (device) {
            waveOutReset(device);
            if (header.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(device, &header, sizeof(header));
            waveOutClose(device);
        }
    }
    MMRESULT start(UINT index, QByteArray &wave) {
        WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
        auto result = waveOutOpen(&device, index, &format, 0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) return result;
        header.lpData = wave.data() + 44;
        header.dwBufferLength = DWORD(wave.size() - 44);
        result = waveOutPrepareHeader(device, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) return result;
        header.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
        header.dwLoops = MAXDWORD;
        return waveOutWrite(device, &header, sizeof(header));
    }
};
class Pattern : public QWidget {
    QTimer timer_;
    quint64 frame_ = 0;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(14, 18, 24));
        p.setPen(QColor(220, 225, 230));
        p.setFont(QFont("Geist Mono", 11));
        p.drawText(24, 36,
                   QString("Capture verification / frame %1 / %2 ms")
                       .arg(frame_)
                       .arg(QDateTime::currentMSecsSinceEpoch()));
        for (int i = 0; i < 64; ++i) {
            int x = int((frame_ * 5 + quint64(i) * 137) % quint64(width()));
            int y = 100 + (i * 79) % (height() - 130);
            p.fillRect(x, y, 3 + i % 7, 3 + i % 5, QColor::fromHsv(i * 23 % 360, 180, 170));
        }
        p.fillRect(int(frame_ * 8 % quint64(width())), 60, 32, 30, QColor(70, 210, 135));
        p.setPen(QColor(150, 160, 170));
        for (int y = 150; y < height(); y += 16)
            p.drawText(24, y, "HUD 0123456789  Small text / motion / dark background");
        for (int bit = 0; bit < 16; ++bit)
            p.fillRect(24 + bit * 18, height() - 28, 14, 14, (frame_ >> bit) & 1 ? Qt::white : Qt::black);
    }

  public:
    Pattern() {
        setWindowTitle("Capture verification");
        setWindowFlag(Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
        const auto area = QGuiApplication::primaryScreen()->availableGeometry().adjusted(20, 40, -20, -20);
        resize(QSize(1280, 720).boundedTo(area.size()));
        move(area.center() - rect().center());
        connect(&timer_, &QTimer::timeout, this, [this] {
            ++frame_;
            update();
        });
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.start(16);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    for (const auto &file : {"InterVariable.ttf", "GeistMono-Regular.ttf"}) {
        const int font = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() + "/assets/fonts/" + file);
        if (font < 0)
            return 2;
        if (QString(file).startsWith("Inter"))
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).value(0), 10));
    }
    QTimer stop;
    const auto args = app.arguments();
    const auto value = [&](const QString &option) {
        const int index = args.indexOf(option);
        return index >= 0 ? args.value(index + 1) : QString();
    };
    QJsonArray devices;
    UINT output = WAVE_MAPPER;
    const auto requested = value("--output-device");
    bool matched = requested.isEmpty();
    for (UINT i = 0; i < waveOutGetNumDevs(); ++i) {
        size_t bytes = 0;
        const auto handle = reinterpret_cast<HWAVEOUT>(UINT_PTR(i));
        if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEIDSIZE, DWORD_PTR(&bytes), 0) ||
            !bytes || bytes > 65536)
            continue;
        std::vector<wchar_t> identity(bytes / sizeof(wchar_t) + 1);
        if (waveOutMessage(handle, DRV_QUERYFUNCTIONINSTANCEID, DWORD_PTR(identity.data()), bytes))
            continue;
        WAVEOUTCAPSW caps{};
        waveOutGetDevCapsW(i, &caps, sizeof(caps));
        const auto id = QString::fromWCharArray(identity.data());
        devices.append(QJsonObject{{"index", int(i)}, {"id", id}, {"name", QString::fromWCharArray(caps.szPname)}});
        if (!requested.isEmpty() && requested.compare(id, Qt::CaseInsensitive) == 0) {
            output = i;
            matched = true;
        }
    }
    QJsonObject report{{"devices", devices}, {"requestedDevice", requested}, {"matched", matched}};
    const auto writeReport = [&] {
        if (const auto path = value("--report"); !path.isEmpty()) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(report).toJson());
                file.commit();
            }
        }
    };
    writeReport();
    if (args.contains("--list-audio"))
        return 0;
    if (!matched)
        return 5;
    const int stopOption = args.indexOf("--stop-file");
    if (stopOption >= 0) {
        const auto path = args.value(stopOption + 1);
        QObject::connect(&stop, &QTimer::timeout, &app, [&, path] {
            if (QFileInfo::exists(path))
                app.quit();
        });
        stop.start(100);
    }
    QByteArray wave;
    auto u16 = [&](quint16 v) {
        wave.append(char(v));
        wave.append(char(v >> 8));
    };
    auto u32 = [&](quint32 v) {
        u16(quint16(v));
        u16(quint16(v >> 16));
    };
    int frames = 48000;
    wave.append("RIFF", 4);
    u32(36 + frames * 4);
    wave.append("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(192000);
    u16(4);
    u16(16);
    wave.append("data", 4);
    u32(frames * 4);
    for (int i = 0; i < frames; ++i) {
        qint16 sample = qint16(std::sin(i * 2 * 3.141592653589793 * 440 / 48000) * 4000);
        u16(quint16(sample));
        u16(quint16(sample));
    }
    WaveTone tone;
    if (app.arguments().contains("--tone")) {
        const auto result = tone.start(output, wave);
        report["renderResult"] = int(result);
        writeReport();
        if (result != MMSYSERR_NOERROR)
            return 4;
    }
    Pattern w;
    w.show();
    ShowWindow(HWND(w.winId()), SW_SHOWNOACTIVATE);
    int result = app.exec();
    if (tone.device) {
        MMTIME played{};
        played.wType = TIME_SAMPLES;
        report["positionResult"] = int(waveOutGetPosition(tone.device, &played, sizeof(played)));
        report["positionType"] = int(played.wType);
        report["playedSamples"] = qint64(played.u.sample);
        writeReport();
    }
    return result;
}
