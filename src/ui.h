#pragma once
#include "jobs.h"
#include "library.h"
#include "platform.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QtWidgets>
namespace clip {
class MainWindow;
class ClipToolbar : public QWidget {
    Q_OBJECT
    friend class UiTests;
    MainWindow *owner_;
    Interval pin_;
    Settings recorded_;
    ApplicationIdentity application_;
    double endpoint_ = 0;
    QComboBox *container_, *fps_, *resolution_, *duration_, *quality_, *encoder_;
    QLabel *available_;
    QPushButton *record_, *save_, *compress_;
    void release();

  protected:
    void keyPressEvent(QKeyEvent *) override;
    void hideEvent(QHideEvent *) override;

  public:
    explicit ClipToolbar(MainWindow *);
    ~ClipToolbar();
    void open();
    void submit(bool compress = false);
};
class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class UiTests;
    friend class ClipToolbar;
    Settings settings_, recorded_;
    Registry registry_;
    Recorder recorder_;
    ExportQueue exports_;
    Library library_;
    Hotkeys hotkeys_;
    ClipToolbar *toolbar_;
    QSystemTrayIcon *tray_;
    QTabWidget *tabs_;
    QLabel *status_, *recordStats_, *preview_, *details_, *encoderDetail_;
    QTableWidget *clips_;
    QListWidget *jobs_;
    QLineEdit *search_;
    QComboBox *applicationFilter_, *source_, *display_, *windows_, *backend_, *encoder_, *recordQuality_,
        *recordFps_, *recordHeight_, *recordContainer_;
    QCheckBox *hdr_;
    QSpinBox *retention_, *storage_;
    QLineEdit *libraryRoot_, *bufferRoot_;
    QTableWidget *audio_;
    QThread *discovery_ = nullptr;
    std::atomic_bool cancelDiscovery_ = false;
    bool exiting_ = false;
    QString selectedPath_;
    QTimer timer_;
    QSet<QString> completedSeen_;
    QMap<QString, QString> notifiedStates_;
    bool libraryRefreshPending_ = false;
    bool resetApproved_ = false;
    QWidget *clipsPage();
    QWidget *recordingPage();
    QWidget *settingsPage();
    void populateEncoders();
    void refreshSources();
    void refreshCaptureSources();
    void refreshLibrary();
    void fillLibrary();
    void selectClip();
    void updateStatus();
    void updateJobs();
    Settings readRecording() const;
    void applyRecording();
    void probeEncoders();
    void showError(const QString &);
    void showPlayer(const QString &);
    void showEditor(int tab, bool sdr = false);
    void calibrate();
    void updateCaptureChoices();
    bool approveSdrRecording(Settings &);
    void closeEvent(QCloseEvent *) override;

  public:
    explicit MainWindow(bool activationOnly = false);
    ~MainWindow();
    void toggleRecording();
    void exitApplication();
    void openClip(const QString &);
    void handleNotificationAction(const QString &);
    bool notificationsEnabled() const {
        return settings_.notifications;
    }
    QList<ExportJob> exportJobs() const {
        return exports_.jobs();
    }
  signals:
    void notificationReady(QString jobId);
};
void applyAppearance(QApplication &);
} // namespace clip
