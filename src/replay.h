#pragma once
#include "common.h"
#include <condition_variable>
namespace clip {
struct Segment {
    QString path;
    double start = 0, end = 0;
    qint64 bytes = 0;
};
struct Interval {
    QString id;
    double start = 0, end = 0, requested = 0;
    QList<Segment> segments;
    double duration() const {
        return qMax(0., end - start);
    }
};
class ReplayBuffer {
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    QList<Segment> segments_;
    QMap<QString, Interval> pins_;
    double epoch_ = 0, latest_ = 0;
    int retention_ = 300;
    qint64 maxBytes_ = 4LL * 1024 * 1024 * 1024;
    bool stopped_ = true;
    bool pinned(const Segment &) const;
    void cleanLocked();

  public:
    void begin(double epoch, int retention, qint64 maxBytes);
    void add(Segment);
    void stop();
    void clear();
    Interval pin(double endpoint, double requested);
    Interval resolve(const QString &id, std::atomic_bool *cancel = nullptr, int timeoutMs = 15000);
    void release(const QString &id);
    double available(double endpoint) const;
    double latest() const;
    qint64 bytes() const;
    int count() const;
};
} // namespace clip
