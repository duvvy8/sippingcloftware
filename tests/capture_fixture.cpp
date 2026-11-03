#include <QtWidgets>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>
class Pattern : public QWidget {
    QTimer timer_;
    quint64 frame_ = 0;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(14, 18, 24));
        p.setPen(QColor(220, 225, 230));
        p.setFont(QFont("Consolas", 11));
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
        resize(1280, 720);
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
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_LOOP);
    Pattern w;
    w.show();
    int result = app.exec();
    PlaySoundW(nullptr, nullptr, 0);
    return result;
}
