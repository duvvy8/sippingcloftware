#pragma once
#include "common.h"
#include <QLocalServer>
namespace clip {
class Instance : public QObject {
    Q_OBJECT
    QString name_;
    QLockFile lock_;
    QLocalServer server_;

  public:
    explicit Instance(QObject *parent = nullptr);
    bool acquire(QString *error = nullptr);
    bool forward(const QJsonObject &, QString *error = nullptr);
  signals:
    void received(QJsonObject message);
};
} // namespace clip
