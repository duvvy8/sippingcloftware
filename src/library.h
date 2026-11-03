#pragma once
#include "media.h"
#include <QThreadPool>
namespace clip {
struct ClipRecord {
    QString path, application, created, relationship = "original", parent, id;
    QJsonObject media, identity;
    QJsonObject json() const;
    static ClipRecord fromJson(const QJsonObject &);
};
class Library : public QObject {
    Q_OBJECT
    QList<ClipRecord> records_;
    std::thread scan_;
    std::atomic_bool cancel_ = false;
    QThreadPool thumbnails_;
    QSet<QString> pending_;

  public:
    explicit Library(QObject *parent = nullptr);
    ~Library();
    void refresh(const QString &root);
    const QList<ClipRecord> &records() const {
        return records_;
    }
    void requestThumbnail(const QString &path);
    bool rename(const QString &path, const QString &newName, QString *error);
  signals:
    void refreshed();
    void status(QString);
    void thumbnailReady(QString path, QImage image);
};
} // namespace clip
