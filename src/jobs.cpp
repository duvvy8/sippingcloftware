#include "jobs.h"
#include "platform.h"
#include "compression.h"
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
                                const QString &exe, bool compress) {
    ExportJob j;
    j.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    j.source = source;
    j.kind = compress ? "save-compress" : "save";
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
        buffer_->narrow(source.id, source.start, source.end);
        changed_.notify_all();
    }
    emit jobChanged(j.id);
    return j.id;
}
QString ExportQueue::submitCompression(const QString &original, const QString &encoder) {
    ExportJob j;
    j.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    j.kind = "compress";
    j.original = original;
    j.settings.encoder = encoder;
    j.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    j.details["sourceIdentity"] = fileIdentity(original);
    if (j.details["sourceIdentity"].toObject().isEmpty())
        throw std::runtime_error("Original is unavailable");
    auto sidecar = readJson(original + ".clip.json");
    j.application = sidecar["application"].toString(QFileInfo(original).dir().dirName());
    j.executable = sidecar["executable"].toString();
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
QString ExportQueue::submitEdit(const QString &original, const EditRequest &request, const QString &encoder,
                                const QString &quality) {
    ExportJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.kind = "edit";
    job.original = original;
    job.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    auto sidecar = readJson(original + ".clip.json");
    job.application = sidecar["application"].toString(QFileInfo(original).dir().dirName());
    job.executable = sidecar["executable"].toString();
    job.settings.encoder = encoder;
    job.settings.quality = quality;
    job.details = {{"edit", request.json()}, {"sourceIdentity", fileIdentity(original)}};
    if (job.details["sourceIdentity"].toObject().isEmpty())
        throw std::runtime_error("Original is unavailable");
    job.output = uniquePath(QFileInfo(original).absolutePath(),
                            QFileInfo(original).completeBaseName() + "_edited_" + job.id.left(8),
                            request.sdr ? "mp4" : QFileInfo(original).suffix());
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            throw std::runtime_error("Export queue is stopping");
        jobs_ << job;
        try {
            persistLocked();
        } catch (...) {
            jobs_.removeLast();
            throw;
        }
        changed_.notify_all();
    }
    emit jobChanged(job.id);
    return job.id;
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
bool ExportQueue::retryCopy(const QString &id, QString *error) {
    ExportJob job;
    {
        std::lock_guard lock(mutex_);
        for (const auto &j : jobs_)
            if (j.id == id && j.terminal && j.details["copyPending"].toBool())
                job = j;
    }
    if (job.id.isEmpty() || job.output.isEmpty() ||
        fileIdentity(job.output) != job.details["outputIdentity"].toObject()) {
        if (error)
            *error = "The saved output changed or this job has no pending clipboard copy";
        return false;
    }
    if (!copyFile(job.output, error))
        return false;
    {
        std::lock_guard lock(mutex_);
        for (auto &j : jobs_)
            if (j.id == id) {
                j.details["copyPending"] = false;
                j.details["copied"] = true;
            }
        persistLocked();
    }
    bool alreadySmall = job.details["alreadySmall"].toBool();
    update(id, alreadySmall ? "Already within limit" : "Complete",
           alreadySmall ? "Original already meets the limit and was copied to clipboard."
                 : "File compressed and copied to clipboard. Delete unreduced file?",
           true);
    return true;
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
                try {
                    buffer_->release(j.source.id);
                } catch (const std::exception &e) {
                    qWarning("Replay pin release at shutdown: %s", e.what());
                }
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
            if (job.kind == "edit") {
                if (fileIdentity(job.original) != job.details["sourceIdentity"].toObject())
                    throw std::runtime_error("Original changed since this edit was queued");
                update(job.id, "Encoding", "Creating a nondestructive edited copy");
                QElapsedTimer throttle;
                throttle.start();
                auto info =
                    editMedia(job.original, job.output, EditRequest::fromJson(job.details["edit"].toObject()),
                              job.settings.encoder, job.settings.quality, &cancel_, [&](double progress) {
                                  if (throttle.elapsed() < 250)
                                      return;
                                  throttle.restart();
                                  {
                                      std::lock_guard lock(mutex_);
                                      for (auto &j : jobs_)
                                          if (j.id == job.id)
                                              j.progress = progress;
                                  }
                                  emit jobChanged(job.id);
                              });
                if (fileIdentity(job.original) != job.details["sourceIdentity"].toObject())
                    throw std::runtime_error(
                        "Original changed while the edit was processing; output retained for inspection");
                if (!writeJson(job.output + ".clip.json", {{"id", job.id},
                                                           {"application", job.application},
                                                           {"executable", job.executable},
                                                           {"created", job.created},
                                                           {"relationship", "edited"},
                                                           {"parent", job.original},
                                                           {"media", info.json()}}))
                    throw std::runtime_error("Edited file saved, but metadata could not be persisted");
                update(job.id, "Complete", "Edited copy saved; original retained", true);
            } else if (job.kind != "compress") {
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
                QJsonObject sidecar{{"id", job.id},
                                    {"application", job.application},
                                    {"executable", job.executable},
                                    {"created", job.created},
                                    {"relationship", "original"},
                                    {"media", info.json()}};
                if (!writeJson(job.original + ".clip.json", sidecar))
                    throw std::runtime_error("Clip saved, but metadata could not be written");
                buffer_->release(job.source.id);
                if (job.kind == "save")
                    update(job.id, "Complete", "Clip saved and completely decoded", true);
            }
            if (job.kind == "compress" || job.kind == "save-compress") {
                if (job.kind == "compress" &&
                    fileIdentity(job.original) != job.details["sourceIdentity"].toObject())
                    throw std::runtime_error("Original changed since compression was queued");
                auto originalIdentity = fileIdentity(job.original);
                auto output =
                    uniquePath(QFileInfo(job.original).absolutePath(),
                               QFileInfo(job.original).completeBaseName() + "_compressed_" + job.id.left(8),
                               QFileInfo(job.original).suffix());
                QElapsedTimer throttle;
                throttle.start();
                QString lastState, lastMessage;
                auto result = compressMedia(job.original, output, job.settings.encoder, &cancel_,
                                            [&](QString state, QString message, double progress) {
                                                if (state == lastState && message == lastMessage &&
                                                    throttle.elapsed() < 250)
                                                    return;
                                                throttle.restart();
                                                lastState = state;
                                                lastMessage = message;
                                                {
                                                    std::lock_guard lock(mutex_);
                                                    for (auto &j : jobs_)
                                                        if (j.id == job.id)
                                                            j.progress = progress;
                                                }
                                                update(job.id, state, message);
                                            });
                {
                    std::lock_guard lock(mutex_);
                    for (auto &j : jobs_)
                        if (j.id == job.id) {
                            j.output = result.output;
                            j.details["originalIdentity"] = originalIdentity;
                            j.details["attempts"] = result.attempts;
                            j.details["quality"] = result.quality.json();
                            j.details["limitingReason"] = result.reason;
                            j.details["alreadySmall"] = result.state == "Already within limit";
                            if (!result.output.isEmpty())
                                j.details["outputIdentity"] = fileIdentity(result.output);
                        }
                    persistLocked();
                }
                if (result.state == "Cannot meet constraints") {
                    update(job.id, result.state, "File was too big to reduce to 20MB. " + result.reason,
                           true);
                } else {
                    bool alreadySmall = result.state == "Already within limit";
                    if (!alreadySmall &&
                        !writeJson(result.output + ".clip.json", {{"id", job.id},
                                                                  {"application", job.application},
                                                                  {"executable", job.executable},
                                                                  {"created", job.created},
                                                                  {"relationship", "compressed"},
                                                                  {"parent", job.original},
                                                                  {"media", result.media.json()},
                                                                  {"quality", result.quality.json()}}))
                        throw std::runtime_error(
                            "Compressed copy saved, but metadata could not be persisted");
                    update(job.id, "Copying", "Copying the saved file to the clipboard");
                    QString error;
                    bool copied = copyFile(result.output, &error);
                    {
                        std::lock_guard lock(mutex_);
                        for (auto &j : jobs_)
                            if (j.id == job.id) {
                                j.details["copyPending"] = !copied;
                                j.details["deleteDecision"] = alreadySmall ? "Not applicable" : "Pending";
                                j.details["copied"] = copied;
                            }
                        persistLocked();
                    }
                    if (!copied)
                        update(job.id, alreadySmall ? "Failed" : "Compressed but clipboard copy failed",
                               (alreadySmall ? "Original already fits, but could not copy. "
                                             : "Compressed, but could not copy. ") +
                                   error,
                               true);
                    else
                        update(job.id, alreadySmall ? "Already within limit" : "Complete",
                               alreadySmall
                                   ? "Original already meets the limit and was copied to clipboard."
                                   : "File compressed and copied to clipboard. Delete unreduced file?",
                               true);
                }
            }
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
