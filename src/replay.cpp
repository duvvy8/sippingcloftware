#include "replay.h"
#include "platform.h"
#include <stdexcept>
namespace clip {
static void removeOwned(const Segment &s) {
    if (!QFileInfo::exists(s.path))
        return;
    if (!s.identity.isEmpty() && fileIdentity(s.path) != s.identity)
        throw std::runtime_error(
            "Replay data changed outside the application; it was preserved. Review the replay folder.");
    if (!QFile::remove(s.path))
        throw std::runtime_error("Cannot remove expired replay data");
}
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
        removeOwned(s);
    segments_.clear();
    epoch_ = latest_ = epoch;
    retention_ = qMax(1, retention);
    maxBytes_ = qMax(qint64(1), maxBytes);
    activeBytes_ = 0;
    stopped_ = false;
}
void ReplayBuffer::cleanLocked() {
    qint64 total = activeBytes_;
    for (const auto &s : segments_)
        total += s.bytes;
    for (int i = 0; i < segments_.size();) {
        const auto &s = segments_[i];
        if ((s.end <= latest_ - retention_ || total > maxBytes_) && !pinned(s)) {
            removeOwned(s);
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
    if (s.identity.isEmpty())
        s.identity = fileIdentity(s.path);
    activeBytes_ = 0;
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
        removeOwned(s);
    segments_.clear();
}
double ReplayBuffer::available(double end) const {
    std::lock_guard lock(mutex_);
    if (stopped_)
        end = qMin(end, latest_);
    double earliest = earliestLocked(end);
    return qMax(0., qMin(double(retention_), end - earliest));
}
Interval ReplayBuffer::pin(double endpoint, double requested) {
    std::lock_guard lock(mutex_);
    if (requested <= 0)
        throw std::runtime_error("Clip duration must be positive");
    if (stopped_ && endpoint > latest_ + 1e-6)
        throw std::runtime_error("No footage exists after the recording endpoint");
    double first = earliestLocked(endpoint);
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
    if (latest_ + 1e-6 < p.end)
        throw std::runtime_error("Capture ended before the selected clip endpoint. No shortened original was "
                                 "saved; reopen the toolbar to choose available footage.");
    if (p.end <= p.start)
        throw std::runtime_error("No completed footage covers this clip");
    for (const auto &s : segments_)
        if (s.end > p.start && s.start < p.end) {
            if (!QFileInfo::exists(s.path) || (!s.identity.isEmpty() && fileIdentity(s.path) != s.identity))
                throw std::runtime_error(
                    "Pinned replay data changed or disappeared; no substituted clip was saved");
            p.segments << s;
        }
    if (p.segments.isEmpty())
        throw std::runtime_error("Requested footage is unavailable");
    for (int i = 1; i < p.segments.size(); ++i)
        if (p.segments[i].start - p.segments[i - 1].end > 1e-6)
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
    qint64 n = activeBytes_;
    for (auto &s : segments_)
        n += s.bytes;
    return n;
}
int ReplayBuffer::count() const {
    std::lock_guard lock(mutex_);
    return int(segments_.size());
}
QList<Segment> ReplayBuffer::segments() const {
    std::lock_guard lock(mutex_);
    return segments_;
}
double ReplayBuffer::earliestLocked(double end) const {
    double start = end >= latest_ ? latest_ : end;
    for (auto it = segments_.crbegin(); it != segments_.crend(); ++it) {
        if (it->start >= end)
            continue;
        if (it->end + 1e-6 < start)
            break;
        start = qMin(start, it->start);
    }
    return start;
}
void ReplayBuffer::reserve(qint64 bytes) {
    std::lock_guard lock(mutex_);
    activeBytes_ = qMax(qint64(0), bytes);
    cleanLocked();
}
void ReplayBuffer::narrow(const QString &id, double start, double end) {
    std::lock_guard lock(mutex_);
    if (!pins_.contains(id) || start < pins_[id].start || end > pins_[id].end || end <= start)
        throw std::runtime_error("Invalid pinned clip interval");
    pins_[id].start = start;
    pins_[id].end = end;
}
} // namespace clip
