#include "replay.h"
#include <stdexcept>
namespace clip {
bool ReplayBuffer::pinned(const Segment &s) const {
    for (const auto &p : pins_)
        if (s.end > p.start && s.start < p.end)
            return true;
    return false;
}
void ReplayBuffer::begin(double epoch, int retention, qint64 maxBytes) {
    std::lock_guard lock(mutex_);
    if (!pins_.isEmpty())
        throw std::runtime_error(
            "Pending clips still own buffer segments; wait for exports before resetting capture");
    for (const auto &s : segments_)
        QFile::remove(s.path);
    segments_.clear();
    epoch_ = latest_ = epoch;
    retention_ = qMax(1, retention);
    maxBytes_ = qMax(qint64(1), maxBytes);
    stopped_ = false;
}
void ReplayBuffer::cleanLocked() {
    qint64 total = 0;
    for (const auto &s : segments_)
        total += s.bytes;
    for (int i = 0; i < segments_.size();) {
        const auto &s = segments_[i];
        if ((s.end <= latest_ - retention_ || total > maxBytes_) && !pinned(s)) {
            if (!QFile::remove(s.path) && QFileInfo::exists(s.path))
                throw std::runtime_error("Cannot remove expired replay segment");
            total -= s.bytes;
            segments_.removeAt(i);
        } else
            ++i;
    }
    if (total > maxBytes_)
        throw std::runtime_error("Replay storage limit reached while clips are pinned. Recording stopped; "
                                 "queued clips are preserved.");
}
void ReplayBuffer::add(Segment s) {
    std::lock_guard lock(mutex_);
    if (s.end <= s.start || s.bytes <= 0)
        throw std::runtime_error("Invalid replay segment");
    if (!segments_.isEmpty() && s.start < segments_.last().start)
        throw std::runtime_error("Out-of-order replay segment");
    segments_ << s;
    latest_ = qMax(latest_, s.end);
    changed_.notify_all();
    cleanLocked();
}
void ReplayBuffer::stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    changed_.notify_all();
}
void ReplayBuffer::clear() {
    std::lock_guard lock(mutex_);
    if (!pins_.isEmpty())
        return;
    for (const auto &s : segments_)
        QFile::remove(s.path);
    segments_.clear();
}
double ReplayBuffer::available(double end) const {
    std::lock_guard lock(mutex_);
    double earliest = segments_.isEmpty() ? epoch_ : segments_.first().start;
    return qMax(0., qMin(double(retention_), end - earliest));
}
Interval ReplayBuffer::pin(double endpoint, double requested) {
    std::lock_guard lock(mutex_);
    if (requested <= 0)
        throw std::runtime_error("Clip duration must be positive");
    double first = segments_.isEmpty() ? epoch_ : segments_.first().start;
    Interval p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.end = qMax(epoch_, endpoint);
    p.start = qMax(first, qMax(endpoint - requested, endpoint - retention_));
    p.requested = requested;
    if (p.end <= p.start)
        throw std::runtime_error("No footage is buffered yet");
    pins_[p.id] = p;
    return p;
}
Interval ReplayBuffer::resolve(const QString &id, std::atomic_bool *cancel, int timeout) {
    std::unique_lock lock(mutex_);
    QElapsedTimer timer;
    timer.start();
    while (pins_.contains(id) && latest_ + 1e-6 < pins_[id].end && !stopped_) {
        if (cancel && cancel->load())
            throw std::runtime_error("Cancelled");
        if (timer.elapsed() > timeout)
            throw std::runtime_error("Timed out waiting for delayed encoder packets to cover clip endpoint");
        changed_.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (!pins_.contains(id))
        throw std::runtime_error("Clip pin was released");
    auto p = pins_[id];
    p.end = qMin(p.end, latest_);
    if (p.end <= p.start)
        throw std::runtime_error("No completed footage covers this clip");
    for (const auto &s : segments_)
        if (s.end > p.start && s.start < p.end)
            p.segments << s;
    if (p.segments.isEmpty())
        throw std::runtime_error("Requested footage is unavailable");
    for (int i = 1; i < p.segments.size(); ++i)
        if (p.segments[i].start - p.segments[i - 1].end > 0.1)
            throw std::runtime_error("Capture gap intersects clip; refusing an incomplete save");
    return p;
}
void ReplayBuffer::release(const QString &id) {
    std::lock_guard lock(mutex_);
    pins_.remove(id);
    cleanLocked();
}
double ReplayBuffer::latest() const {
    std::lock_guard lock(mutex_);
    return latest_;
}
qint64 ReplayBuffer::bytes() const {
    std::lock_guard lock(mutex_);
    qint64 n = 0;
    for (auto &s : segments_)
        n += s.bytes;
    return n;
}
int ReplayBuffer::count() const {
    std::lock_guard lock(mutex_);
    return int(segments_.size());
}
} // namespace clip
