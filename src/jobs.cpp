#include "jobs.h"
namespace clip {
QJsonObject ExportJob::json() const {
    QJsonArray segments;
    for (auto &s : source.segments)
        segments << QJsonObject{{"path", s.path}, {"start", s.start}, {"end", s.end}, {"bytes", s.bytes}};
    return {{"id", id},
            {"state", state},
            {"message", message},
            {"application", application},
            {"executable", executable},
            {"original", original},
            {"output", output},
            {"kind", kind},
            {"created", created},
            {"settings", settings.json()},
            {"source", QJsonObject{{"id", source.id},
                                   {"start", source.start},
                                   {"end", source.end},
                                   {"requested", source.requested},
                                   {"segments", segments}}},
            {"terminal", terminal},
            {"progress", progress},
            {"details", details}};
}
ExportJob ExportJob::fromJson(const QJsonObject &o) {
    ExportJob j;
    j.id = o["id"].toString();
    j.state = o["state"].toString();
    j.message = o["message"].toString();
    j.application = o["application"].toString();
    j.executable = o["executable"].toString();
    j.original = o["original"].toString();
    j.output = o["output"].toString();
    j.kind = o["kind"].toString();
    j.created = o["created"].toString();
    j.settings.loadObject(o["settings"].toObject());
    auto s = o["source"].toObject();
    j.source.id = s["id"].toString();
    j.source.start = s["start"].toDouble();
    j.source.end = s["end"].toDouble();
    j.source.requested = s["requested"].toDouble();
    for (auto v : s["segments"].toArray()) {
        auto a = v.toObject();
        j.source.segments.append(
            {a["path"].toString(), a["start"].toDouble(), a["end"].toDouble(), a["bytes"].toInteger()});
    }
    j.terminal = o["terminal"].toBool();
    j.progress = o["progress"].toDouble();
    j.details = o["details"].toObject();
    return j;
}
ExportQueue::ExportQueue(ReplayBuffer *b, QObject *parent, QString storage)
    : QObject(parent), buffer_(b), folder_(storage.isEmpty() ? stateDir() + "/jobs" : storage) {
    QDir().mkpath(folder_);
    for (auto f : QDir(folder_).entryList({"*.json"}, QDir::Files)) {
        auto j = ExportJob::fromJson(readJson(QDir(folder_).filePath(f)));
        if (j.id.isEmpty())
            continue;
        if (!j.terminal) {
            j.state = "Failed";
            j.message =
                "Interrupted by application shutdown or crash. Any finalized original has been retained.";
            j.terminal = true;
            QFile::remove(j.original + ".partial");
            writeJson(QDir(folder_).filePath(j.id + ".json"), j.json());
        }
        jobs_ << j;
    }
    worker_ = std::thread([this] { run(); });
}
ExportQueue::~ExportQueue() {
    shutdown();
}
void ExportQueue::persistLocked() {
    for (auto &j : jobs_)
        if (!writeJson(QDir(folder_).filePath(j.id + ".json"), j.json()))
            throw std::runtime_error(
                "Cannot persist export job. Check available disk space and folder access.");
}
QString ExportQueue::submitSave(const Interval &source, const Settings &s, const QString &app,
                                const QString &exe) {
    ExportJob j;
    j.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    j.source = source;
    j.settings = s;
    j.application = app;
    j.executable = exe;
    j.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString root = s.folderOverrides.value(exe, QDir(s.library).filePath(safeName(app)));
    j.original = uniquePath(
        root, QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + '_' + j.id.left(8),
        s.container);
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            throw std::runtime_error("Export queue is stopping");
        jobs_ << j;
        try {
            persistLocked();
        } catch (...) {
            jobs_.removeLast();
            throw;
        }
        changed_.notify_all();
    }
    emit jobChanged(j.id);
    return j.id;
}
QList<ExportJob> ExportQueue::jobs() const {
    std::lock_guard lock(mutex_);
    return jobs_;
}
void ExportQueue::update(const QString &id, const QString &state, const QString &message, bool terminal) {
    {
        std::lock_guard lock(mutex_);
        for (auto &j : jobs_)
            if (j.id == id) {
                j.state = state;
                j.message = message;
                j.terminal = terminal;
                if (terminal)
                    j.progress = state == "Complete" ? 1. : j.progress;
            }
        persistLocked();
    }
    emit jobChanged(id);
}
void ExportQueue::cancel(const QString &id) {
    QString pin;
    {
        std::lock_guard lock(mutex_);
        if (current_ == id)
            cancel_ = true;
        else
            for (auto &j : jobs_)
                if (j.id == id && !j.terminal) {
                    j.state = "Cancelled";
                    j.message = "Cancelled before saving. Existing recordings were retained.";
                    j.terminal = true;
                    pin = j.source.id;
                    persistLocked();
                }
    }
    if (!pin.isEmpty())
        try {
            buffer_->release(pin);
        } catch (const std::exception &e) {
            emit queueError(QString::fromUtf8(e.what()));
        }
    emit jobChanged(id);
}
void ExportQueue::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        stopping_ = true;
        cancel_ = true;
        for (auto &j : jobs_)
            if (!j.terminal && j.id != current_) {
                j.state = "Cancelled";
                j.terminal = true;
                j.message = "Application exited before this export started.";
                buffer_->release(j.source.id);
            }
        try {
            persistLocked();
        } catch (const std::exception &e) {
            qWarning("Export state could not be persisted at shutdown: %s", e.what());
        }
        changed_.notify_all();
    }
    if (worker_.joinable())
        worker_.join();
}
void ExportQueue::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        ExportJob job;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [&] {
                if (stopping_)
                    return true;
                for (auto &j : jobs_)
                    if (!j.terminal && j.state == "Queued")
                        return true;
                return false;
            });
            if (stopping_)
                break;
            for (auto &j : jobs_)
                if (!j.terminal && j.state == "Queued") {
                    job = j;
                    break;
                }
            current_ = job.id;
            cancel_ = false;
        }
        try {
            update(job.id, "Saving original", "Waiting for final packets at the selected endpoint");
            auto resolved = buffer_->resolve(job.source.id, &cancel_);
            resolved.start = qMax(resolved.start, job.source.start);
            resolved.end = qMin(resolved.end, job.source.end);
            QList<Segment> chosen;
            for (auto &s : resolved.segments)
                if (s.end > resolved.start && s.start < resolved.end)
                    chosen << s;
            resolved.segments = chosen;
            {
                std::lock_guard lock(mutex_);
                for (auto &j : jobs_)
                    if (j.id == job.id)
                        j.source = resolved;
                persistLocked();
            }
            saveInterval(resolved, job.settings, job.original, &cancel_);
            auto info = inspect(job.original);
            QJsonObject sidecar{
                {"id", job.id},           {"application", job.application}, {"executable", job.executable},
                {"created", job.created}, {"relationship", "original"},     {"media", info.json()}};
            if (!writeJson(job.original + ".clip.json", sidecar))
                throw std::runtime_error("Clip saved, but metadata could not be written");
            update(job.id, "Complete", "Clip saved and completely decoded", true);
        } catch (const std::exception &e) {
            try {
                update(job.id, cancel_ ? "Cancelled" : "Failed", QString::fromUtf8(e.what()), true);
            } catch (
                ...) { /* Preserve original and in-memory failed state even if persistence is unavailable. */
            }
        }
        try {
            buffer_->release(job.source.id);
        } catch (const std::exception &e) {
            emit queueError(QString::fromUtf8(e.what()));
        }
        {
            std::lock_guard lock(mutex_);
            current_.clear();
        }
    }
}
} // namespace clip
