#pragma once
#include "recorder.h"
#include "edit.h"
namespace clip {
struct ExportJob {
    QString id, state = "Queued", message, application = "Desktop", executable, original, output,
                kind = "save", created;
    Settings settings;
    Interval source;
    bool terminal = false;
    double progress = 0;
    QJsonObject details;
    QJsonObject json() const;
    static ExportJob fromJson(const QJsonObject &);
};
class ExportQueue : public QObject {
    Q_OBJECT
    ReplayBuffer *buffer_;
    QString folder_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    QList<ExportJob> jobs_;
    std::thread worker_;
    bool stopping_ = false;
    QString current_;
    std::atomic_bool cancel_ = false;
    void run();
    void persistLocked();
    void update(const QString &, const QString &, const QString &message = {}, bool terminal = false);

  public:
    explicit ExportQueue(ReplayBuffer *, QObject *parent = nullptr, QString storage = {});
    ~ExportQueue();
    QString submitSave(const Interval &, const Settings &, const QString &application,
                       const QString &executable, bool compress = false);
    QString submitCompression(const QString &original, const QString &encoder);
    QString submitEdit(const QString &original, const EditRequest &, const QString &encoder,
                       const QString &quality);
    QList<ExportJob> jobs() const;
    void cancel(const QString &);
    bool retryCopy(const QString &, QString *error = nullptr);
    void shutdown();
  signals:
    void jobChanged(QString id);
    void queueError(QString reason);
};
} // namespace clip
