#pragma once
#include "common.h"
#include <QAbstractNativeEventFilter>
namespace clip {
struct ApplicationIdentity {
    QString name = "Desktop", executable;
    quintptr window = 0;
};
ApplicationIdentity foregroundApplication();
ApplicationIdentity windowApplication(quintptr);
QList<QPair<quintptr, QString>> captureWindows();
bool copyFile(const QString &, QString *error = nullptr, int attempts = 8);
void showInFolder(const QString &);
bool registerStartup(bool enabled, QString *error = nullptr);
bool excludeFromCapture(quintptr);
QJsonObject fileIdentity(const QString &);
class Hotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    struct Binding {
        int id = 0;
        QString text;
    };
    QMap<QString, Binding> bindings_;
    int nextId_ = 100;

  public:
    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys();
    static bool parse(const QString &, UINT &modifiers, UINT &key);
    bool assign(const QString &command, const QString &shortcut, QString *error = nullptr);
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void activated(QString command);
    void environmentChanged(QString reason);
};
} // namespace clip
