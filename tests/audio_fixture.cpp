#include "audio.h"
#include <cmath>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const auto value = [&](const QString &key) { return args.value(args.indexOf(key) + 1); };
    QJsonObject report;
    int result = 0;
    try {
        clip::AudioSource source;
        source.id = value("--device");
        source.kind = "system";
        clip::AudioEngine fast, delayed;
        fast.start({source});
        delayed.start({source});
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        QFile early(value("--output") + "-50ms.f32"), late(value("--output") + "-250ms.f32");
        if (!early.open(QIODevice::WriteOnly) || !late.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot create audio diagnostic output");
        double energy[2]{}, peak[2]{};
        const double epoch = clip::clockSeconds();
        for (int frame = 0; frame < 200; ++frame) {
            const double target = epoch + frame * .01;
            while (clip::clockSeconds() < target + .25)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto a = fast.read(target + .20, 480).value(0);
            const auto b = delayed.read(target, 480).value(0);
            const std::vector<float> *blocks[]{&a, &b};
            for (int index = 0; index < 2; ++index)
                for (float sample : *blocks[index]) {
                    energy[index] += double(sample) * sample;
                    peak[index] = qMax(peak[index], std::abs(double(sample)));
                }
            early.write(reinterpret_cast<const char *>(a.data()), qint64(a.size() * sizeof(float)));
            late.write(reinterpret_cast<const char *>(b.data()), qint64(b.size() * sizeof(float)));
        }
        if (!fast.error().isEmpty() || !delayed.error().isEmpty())
            throw std::runtime_error((fast.error() + delayed.error()).toStdString());
        for (int index = 0; index < 2; ++index)
            report[index ? "250ms" : "50ms"] = QJsonObject{
                {"rmsDb", 10 * std::log10(qMax(energy[index] / (200 * 960), 1e-20))}, {"peak", peak[index]}};
        report["discontinuities"] = qint64(fast.discontinuities());
    } catch (const std::exception &error) {
        report["error"] = QString::fromUtf8(error.what());
        result = 1;
    }
    if (!clip::writeJson(value("--result"), report))
        return 2;
    return result;
}
