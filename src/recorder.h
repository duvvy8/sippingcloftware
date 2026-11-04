#pragma once
#include "audio.h"
#include "capture.h"
#include "replay.h"
#include <condition_variable>
namespace clip {
struct RecordingStats {
    bool active = false;
    double started = 0, elapsed = 0;
    quint64 captured = 0, encoded = 0, dropped = 0, audioDiscontinuities = 0;
    int backlog = 0, width = 0, height = 0, fps = 0;
    qint64 bytes = 0;
    QString encoder, backend, error;
    QJsonObject json() const;
};
class Recorder {
    ReplayBuffer buffer_;
    std::thread worker_;
    std::atomic_bool stop_ = false, active_ = false;
    std::atomic<double> stopAt_ = 0;
    QString storageToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mutable std::mutex mutex_;
    RecordingStats stats_;
    std::condition_variable ready_;
    bool initialized_ = false;
    void run(Settings settings, Registry registry);

  public:
    ~Recorder();
    void start(const Settings &, const Registry &);
    void stop();
    bool active() const {
        return active_;
    }
    RecordingStats stats() const;
    ReplayBuffer &buffer() {
        return buffer_;
    }
};
// Used by the serial export queue. Does not release the caller's interval pin.
QString saveInterval(const Interval &, const Settings &, const QString &output,
                     std::atomic_bool *cancel = nullptr);
} // namespace clip
