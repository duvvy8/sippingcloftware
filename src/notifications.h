#pragma once
#include "jobs.h"
namespace clip {
struct NotificationAction {
    QString job, action;
    bool valid = false;
};
NotificationAction parseNotificationAction(const QString &);
QString notificationXml(const ExportJob &);
QString savedNotificationXml(const QString &id, const QJsonObject &media);
class Notifications : public QObject {
    Q_OBJECT
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    static constexpr const wchar_t *AppId = L"Local.DesktopClipper";
    static constexpr const wchar_t *ClassId = L"{75C51036-3E5E-49CE-A985-5949A51661F8}";
    explicit Notifications(QObject *parent = nullptr);
    ~Notifications();
    // True means the Windows API accepted submission; Windows can still suppress presentation.
    bool show(const ExportJob &, QString *error = nullptr, bool suppressPopup = false);
    bool showSaved(const QString &id, const QJsonObject &media, QString *error = nullptr,
                   bool suppressPopup = false);
    static bool registerShell(bool install, QString *error = nullptr);
  signals:
    void activated(QString arguments);
};
} // namespace clip
