#pragma once
#include "common.h"
namespace clip {
struct AudioDevice {
    QString id, name;
    bool input = false, defaultDevice = false;
};
struct AudioBlock {
    double time = 0;
    std::vector<float> samples;
};
class AudioEngine {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    AudioEngine();
    ~AudioEngine();
    static QList<AudioDevice> devices();
    static bool processCaptureSupported();
    static void validateSources(const QList<AudioSource> &);
    void start(const QList<AudioSource> &);
    void stop();
    QList<std::vector<float>> read(double start, int frames);
    int tracks() const;
    QString error() const;
    QString notice() const;
    quint64 discontinuities() const;
};
} // namespace clip
