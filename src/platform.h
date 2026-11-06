#pragma once
#include "common.h"
#include <QAbstractNativeEventFilter>
namespace clip {
QJsonObject processIdentity(quint32 pid);
bool processTreeContains(quint32 ancestor, quint32 process);
struct ApplicationIdentity {
    QString name = "Desktop", executable;
    QString processCreated;
    quint32 pid = 0;
    quintptr window = 0;
};
ApplicationIdentity windowApplication(quintptr);
HWND fullscreenCaptureWindow(HMONITOR, HWND windowCapture = nullptr);
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
        bool registered = false;
    };
    QMap<QString, Binding> bindings_;
    int nextId_ = 100;
    bool suspended_ = false;

  public:
    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys();
    static bool parse(const QString &, UINT &modifiers, UINT &key);
    bool assign(const QString &command, const QString &shortcut, QString *error = nullptr);
    bool setSuspended(bool suspended, QString *error = nullptr);
    bool suspended() const { return suspended_; }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void activated(QString command);
    void environmentChanged(QString reason);
};
} // namespace clip
