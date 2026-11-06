#include "ui.h"
#include "notifications.h"
#include "editor.h"
#include "hud_backdrop.h"
#include <QStandardItemModel>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace clip {
namespace {
QScreen *requestedUiScreen() {
    const auto args = QCoreApplication::arguments();
    const int option = args.indexOf("--screen");
    const auto name = option >= 0 ? args.value(option + 1) : qEnvironmentVariable("CLIPPER_UI_SCREEN");
    if (name == "secondary") {
        for (auto *screen : QGuiApplication::screens())
            if (screen != QGuiApplication::primaryScreen())
                return screen;
    } else if (!name.isEmpty()) {
        for (auto *screen : QGuiApplication::screens())
            if (screen->name().compare(name, Qt::CaseInsensitive) == 0)
                return screen;
    }
    return nullptr;
}
bool backgroundUi() {
    return QCoreApplication::arguments().contains("--no-focus") ||
           qEnvironmentVariableIsSet("CLIPPER_BACKGROUND_UI");
}
class WindowPlacement : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Polish)
            if (auto *window = qobject_cast<QWidget *>(object); window && window->isWindow()) {
                if (backgroundUi())
                    window->setAttribute(Qt::WA_ShowWithoutActivating);
                if (auto *screen = requestedUiScreen(); screen && window->objectName() != "clipToolbar" &&
                    window->windowTitle() != "Clipping toolbar dimmer") {
                    const auto area = screen->availableGeometry().adjusted(8, 30, -8, -8);
                    window->setMinimumSize(window->minimumSize().boundedTo(area.size()));
                    window->setMaximumSize(area.size());
                    window->resize(window->size().boundedTo(area.size()));
                    window->move(area.center() - window->rect().center());
                }
            }
        return false;
    }
};
class ShortcutEdit : public QKeySequenceEdit {
    std::function<void(bool)> capture_;
  public:
    ShortcutEdit(const QKeySequence &sequence, std::function<void(bool)> capture)
        : QKeySequenceEdit(sequence), capture_(std::move(capture)) {}
  protected:
    void focusInEvent(QFocusEvent *event) override {
        capture_(true);
        QKeySequenceEdit::focusInEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override {
        QKeySequenceEdit::focusOutEvent(event);
        capture_(false);
    }
};
QFont mono() {
    return QFont("Geist Mono", 10);
}
QPushButton *button(const QString &label, QLayout *layout, std::function<void()> action) {
    auto *b = new QPushButton(label);
    b->setMinimumHeight(32);
    layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, std::move(action));
    return b;
}
QWidget *scrollPage(QWidget *child) {
    auto *s = new QScrollArea;
    s->setWidgetResizable(true);
    s->setFrameShape(QFrame::NoFrame);
    s->setWidget(child);
    return s;
}
QString durationText(double seconds) {
    int n = qMax(0, int(std::round(seconds)));
    return QString("%1:%2").arg(n / 60).arg(n % 60, 2, 10, QChar('0'));
}
QString sizeText(qint64 bytes) {
    return bytes >= 1024 * 1024 ? QString::number(double(bytes) / 1048576., 'f', 1) + " MiB"
                                : QString::number(double(bytes) / 1024., 'f', 1) + " KiB";
}
void addChoice(QComboBox *box, const QString &text, const QVariant &value, bool enabled = true,
               const QString &reason = {}) {
    box->addItem(text, value);
    if (auto *model = qobject_cast<QStandardItemModel *>(box->model())) {
        auto *item = model->item(box->count() - 1);
        item->setEnabled(enabled);
        if (!reason.isEmpty())
            item->setToolTip(reason);
    }
}
void selectValue(QComboBox *b, const QVariant &v) {
    int index = b->findData(v);
    if (index >= 0)
        b->setCurrentIndex(index);
}
void populateDisplays(QComboBox *box, const QList<Display> &displays, const QString &identity,
                      int legacyIndex) {
    QSignalBlocker blocker(box);
    box->clear();
    int selected = -1;
    for (int i = 0; i < displays.size(); ++i) {
        const auto &d = displays[i];
        box->addItem(QString("%1 · %2 × %3 · %4 Hz%5")
                         .arg(d.name)
                         .arg(d.width)
                         .arg(d.height)
                         .arg(d.refresh)
                         .arg(d.hdr ? " · HDR" : ""),
                     i);
        box->setItemData(i, d.identity, Qt::UserRole + 1);
        if ((!identity.isEmpty() && d.identity == identity) || (identity.isEmpty() && i == legacyIndex))
            selected = i;
    }
    if (selected < 0) {
        addChoice(
            box, "Selected display unavailable · refresh or reselect", -1, false,
            "The previous display was not found. A different display will not be selected automatically.");
        selected = box->count() - 1;
        box->setItemData(selected, identity, Qt::UserRole + 1);
    }
    box->setCurrentIndex(selected);
}
} // namespace
void applyAppearance(QApplication &app) {
    if (requestedUiScreen() || backgroundUi())
        app.installEventFilter(new WindowPlacement(&app));
    QString interFamily;
    for (const auto &file : {"InterVariable.ttf", "GeistMono-Regular.ttf"}) {
        const int id = QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath() +
                                                         "/assets/fonts/" + file);
        if (id < 0)
            throw std::runtime_error("A required bundled font could not be loaded");
        if (QString(file).startsWith("Inter"))
            interFamily = QFontDatabase::applicationFontFamilies(id).value(0);
    }
    app.setStyle("Fusion");
    app.setFont(QFont(interFamily, 10));
    app.setStyleSheet(R"(
 QWidget { background:#202226; color:#e7e9ed; }
 QMainWindow, QDialog { background:#202226; }
 QLabel { background:transparent; }
 QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QKeySequenceEdit { background:#292c31; border:1px solid #4a4e56; border-radius:5px; padding:6px; selection-background-color:#365c86; }
 QPushButton { background:#30343b; border:1px solid #535963; border-radius:5px; padding:7px 12px; }
 QPushButton:hover { background:#3b414b; border-color:#a8b6c9; }
 QPushButton:pressed { background:#414c5c; }
 QPushButton:disabled { color:#969ba4; background:#272a2f; border-color:#383d45; }
 QPushButton:focus,QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QTableWidget:focus { border:2px solid #8cb6ec; }
 QTabWidget::pane { border:1px solid #444850; }
 QTabBar::tab { background:#25282d; padding:12px 24px; border-bottom:3px solid transparent; }
 QTabBar::tab:selected { background:#30353e; border-bottom:3px solid #8cb6ec; }
 QTableWidget,QListWidget { background:#24272c; alternate-background-color:#2a2e34; border:1px solid #444850; selection-background-color:#365473; gridline-color:#363b43; }
 QHeaderView::section { background:#30343b; padding:8px; border:none; border-right:1px solid #474d57; }
 QGroupBox { border:1px solid #454b55; border-radius:6px; margin-top:14px; padding:14px; }
 QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; }
 QToolTip { color:#edf0f4; background:#343b46; border:1px solid #7c8797; padding:6px; }
 QMenu { background:#292d34; border:1px solid #59616d; }
 QMenu::item { padding:8px 18px; }
 QMenu::item:selected { background:#365473; }
 QCheckBox::indicator { width:18px; height:18px; }
 )");
}
MainWindow::MainWindow(bool activationOnly)
    : exports_(&recorder_.buffer(), this), library_(this), hotkeys_(this) {
    const auto savedSettings = readJson(stateDir() + "/settings.json");
    settings_.loadObject(savedSettings);
    if (QDir(settings_.library) == QDir(legacyClipLibrary()))
        settings_.library = defaultClipLibrary();
    if (!activationOnly && !savedSettings.contains("recordOnLaunch")) {
        const auto devices = AudioEngine::devices();
        for (bool input : {false, true}) {
            const bool configured = std::any_of(settings_.audio.begin(), settings_.audio.end(),
                [input](const AudioSource &source) { return (source.kind == "microphone") == input; });
            if (configured)
                continue;
            for (const auto &device : devices)
                if (device.input == input && device.defaultDevice) {
                    AudioSource source;
                    source.id = device.id;
                    source.name = device.name;
                    source.kind = input ? "microphone" : "system";
                    settings_.audio << source;
                    break;
                }
        }
    }
    for (const auto &job : exports_.jobs())
        if (job.terminal)
            notifiedStates_[job.id] = job.state;
    recorded_ = settings_;
    registry_.discover();
    registry_.loadCache();
    setWindowTitle(AppName);
    resize(1180, 800);
    setMinimumSize(760, 520);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 18, 20, 18);
    tabs_ = new QTabWidget;
    tabs_->addTab(clipsPage(), "Clips");
    tabs_->addTab(recordingPage(), "Recording");
    tabs_->addTab(settingsPage(), "Settings");
    layout->addWidget(tabs_);
    status_ = new QLabel("Recording off · Open the toolbar with " + settings_.overlayHotkey);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setCentralWidget(scrollPage(body));
    toolbar_ = new ClipToolbar(this);
    auto *menu = new QMenu(this);
    auto *openControls = menu->addAction("Open SippingCloftware", this, &MainWindow::showControls);
    menu->setDefaultAction(openControls);
    menu->addAction("Open clip toolbar", toolbar_, &ClipToolbar::open);
    recordAction_ = menu->addAction("Start recording", this, &MainWindow::toggleRecording);
    menu->addSeparator();
    menu->addAction("Exit", this, &MainWindow::exitApplication);
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#e2e9ef"), 1.7));
        p.drawRoundedRect(QRectF(2.5, 4.5, 19, 15), 3, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#a0e1b9"));
        p.drawPolygon(QPolygonF{QPointF(10, 8), QPointF(10, 16), QPointF(16, 12)});
    }
    tray_ = new QSystemTrayIcon(QIcon(icon), this);
    tray_->setToolTip(QString(AppName) + " · Recording off");
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showControls();
    });
    tray_->show();
    QString error;
    if (!hotkeys_.assign("toolbar", settings_.overlayHotkey, &error) ||
        !hotkeys_.assign("record", settings_.recordHotkey, &error))
        QTimer::singleShot(0, this, [this, error] { showError(error); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (exiting_)
            return;
        bool editing = false;
        if (state == Qt::ApplicationActive)
            for (auto *widget = QApplication::focusWidget(); widget; widget = widget->parentWidget())
                if (qobject_cast<QKeySequenceEdit *>(widget)) {
                    editing = true;
                    break;
                }
        QString error;
        if (!hotkeys_.setSuspended(editing, &error))
            status_->setText(error);
    });
    connect(&hotkeys_, &Hotkeys::activated, this, [this](const QString &c) {
        if (c == "toolbar")
            toolbar_->open();
        else
            toggleRecording();
    });
    connect(&hotkeys_, &Hotkeys::environmentChanged, this, [this](const QString &reason) {
        if (recorder_.active())
            recorder_.stop();
        status_->setText(reason);
    });
    connect(&library_, &Library::refreshed, this, &MainWindow::fillLibrary);
    connect(&library_, &Library::thumbnailReady, this, [this](const QString &path, const QImage &image) {
        if (path == selectedPath_ && !image.isNull())
            preview_->setPixmap(QPixmap::fromImage(image).scaled(preview_->size(), Qt::KeepAspectRatio,
                                                                 Qt::SmoothTransformation));
        for (int i = 0; i < clips_->rowCount(); ++i)
            if (clips_->item(i, 0)->data(Qt::UserRole).toString() == path)
                clips_->item(i, 0)->setIcon(QPixmap::fromImage(image));
    });
    connect(&exports_, &ExportQueue::clipSaved, this, &MainWindow::clipSaved);
    connect(&exports_, &ExportQueue::jobChanged, this, [this](const QString &id) {
        updateJobs();
        for (const auto &job : exports_.jobs())
            if (job.id == id && job.terminal && notifiedStates_.value(id) != job.state) {
                notifiedStates_[id] = job.state;
                if (job.kind != "save" || job.state != "Complete")
                    emit notificationReady(id);
            }
        for (const auto &job : exports_.jobs())
            if (job.id == id && job.terminal &&
                (job.state == "Complete" || job.state == "Already within limit" ||
                 job.state == "Compressed but clipboard copy failed") &&
                !completedSeen_.contains(id)) {
                completedSeen_.insert(id);
                if (recorder_.active())
                    libraryRefreshPending_ = true;
                else
                    refreshLibrary();
            }
    });
    connect(&timer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    timer_.start(500);
    populateEncoders();
    refreshSources();
    connect(display_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCaptureChoices);
    connect(source_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCaptureChoices);
    connect(windows_, &QComboBox::currentIndexChanged, this, &MainWindow::updateCaptureChoices);
    updateCaptureChoices();
    refreshLibrary();
    updateJobs();
    if (settings_.autoRecord && !activationOnly)
        QTimer::singleShot(0, this, [this] { startRecording(true); });
}
MainWindow::~MainWindow() {
    exiting_ = true;
    cancelDiscovery_ = true;
    if (discovery_) {
        discovery_->quit();
        discovery_->wait();
    }
    if (toolbar_) {
        delete toolbar_;
        toolbar_ = nullptr;
    }
    recorder_.stop();
    exports_.shutdown();
}
QWidget *MainWindow::clipsPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    auto *filters = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search saved clips");
    search_->setAccessibleName("Search clips");
    applicationFilter_ = new QComboBox;
    applicationFilter_->addItem("All applications");
    filters->addWidget(search_, 1);
    filters->addWidget(applicationFilter_);
    button("Refresh", filters, [this] { refreshLibrary(); });
    v->addLayout(filters);
    auto *split = new QSplitter;
    clips_ = new QTableWidget(0, 7);
    clips_->setHorizontalHeaderLabels(
        {"Clip", "Application", "Captured", "Duration", "Video", "Size", "Type"});
    clips_->setSelectionBehavior(QAbstractItemView::SelectRows);
    clips_->setSelectionMode(QAbstractItemView::SingleSelection);
    clips_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    clips_->setAlternatingRowColors(true);
    clips_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    clips_->horizontalHeader()->setStretchLastSection(true);
    clips_->setIconSize({96, 54});
    clips_->verticalHeader()->hide();
    split->addWidget(clips_);
    auto *right = new QWidget;
    auto *r = new QVBoxLayout(right);
    preview_ = new QLabel("Select a clip to preview");
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(220, 160);
    preview_->setMaximumHeight(280);
    r->addWidget(preview_);
    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setFont(mono());
    r->addWidget(details_);
    auto *actions = new QHBoxLayout;
    button("Play", actions, [this] {
        if (!selectedPath_.isEmpty())
            showPlayer(selectedPath_);
    });
    button("Show in Folder", actions, [this] {
        if (!selectedPath_.isEmpty())
            showInFolder(selectedPath_);
    });
    r->addLayout(actions);
    auto *more = new QHBoxLayout;
    button("Copy File", more, [this] {
        if (selectedPath_.isEmpty())
            return;
        QString error;
        if (!copyFile(selectedPath_, &error))
            showError(error);
        else
            status_->setText("File copied to clipboard");
    });
    button("Rename", more, [this] {
        if (selectedPath_.isEmpty())
            return;
        bool ok = false;
        auto name =
            QInputDialog::getText(this, "Rename clip", "Filename without extension", QLineEdit::Normal,
                                  QFileInfo(selectedPath_).completeBaseName(), &ok);
        if (ok) {
            QString error;
            if (!library_.rename(selectedPath_, name, &error))
                showError(error);
        }
    });
    r->addLayout(more);
    auto *editing = new QHBoxLayout;
    button("Trim", editing, [this] { showEditor(0); });
    button("Crop / Aspect Ratio", editing, [this] { showEditor(1); });
    button("Audio", editing, [this] { showEditor(2); });
    r->addLayout(editing);
    button("SDR Sharing Copy", r, [this] { showEditor(0, true); });
    button("Auto Compress", r, [this] {
        if (selectedPath_.isEmpty())
            return;
        try {
            auto info = inspect(selectedPath_);
            QString encoder = registry_.forCodec(info.codecId, {}, info.hdr);
            if (encoder.isEmpty() && info.bytes > SizeLimit)
                throw std::runtime_error("Test an implementation of this clip's codec before compression");
            exports_.submitCompression(selectedPath_, encoder, settings_.library);
        } catch (const std::exception &e) {
            showError(QString::fromUtf8(e.what()));
        }
    });
    r->addStretch();
    split->addWidget(right);
    split->setSizes({760, 320});
    v->addWidget(split, 3);
    v->addWidget(new QLabel("Export jobs"));
    jobs_ = new QListWidget;
    jobs_->setMaximumHeight(130);
    jobs_->setFont(mono());
    jobs_->setWordWrap(true);
    v->addWidget(jobs_);
    auto *decisions = new QHBoxLayout;
    auto *cancelJob = button("Cancel job", decisions, [this] {
        auto *item = jobs_->currentItem();
        if (item)
            exports_.cancel(item->data(Qt::UserRole).toString());
    });
    auto *retryCopy = button("Retry Copy", decisions, [this] {
        auto *item = jobs_->currentItem();
        if (!item)
            return;
        try {
            QString error;
            if (!exports_.retryCopy(item->data(Qt::UserRole).toString(), &error))
                showError(error);
        } catch (const std::exception &e) {
            showError(QString::fromUtf8(e.what()));
        }
    });
    auto decide = [this](bool recycle) {
        auto *item = jobs_->currentItem();
        if (!item)
            return;
        try {
            QString error;
            if (!exports_.decideOriginal(item->data(Qt::UserRole).toString(), recycle, &error))
                showError(error);
            else if (!recorder_.active())
                refreshLibrary();
        } catch (const std::exception &e) {
            showError(QString::fromUtf8(e.what()));
        }
    };
    auto *yes = button("Yes: Recycle original", decisions, [decide] { decide(true); });
    auto *no = button("No: Keep original", decisions, [decide] { decide(false); });
    yes->setObjectName("recycleOriginal");
    no->setObjectName("retainOriginal");
    v->addLayout(decisions);
    auto updateActions = [this, cancelJob, retryCopy, yes, no] {
        ExportJob selected;
        auto *item = jobs_->currentItem();
        if (item)
            for (const auto &job : exports_.jobs())
                if (job.id == item->data(Qt::UserRole).toString())
                    selected = job;
        bool pending = selected.state == "Complete" && selected.details["copied"].toBool() &&
                       !selected.details["alreadySmall"].toBool() &&
                       QStringList{"Pending", "Recycle failed", "Recycle interrupted"}.contains(
                           selected.details["deleteDecision"].toString());
        cancelJob->setEnabled(!selected.id.isEmpty() && !selected.terminal);
        retryCopy->setEnabled(selected.terminal && selected.details["copyPending"].toBool());
        yes->setEnabled(pending);
        no->setEnabled(pending);
    };
    connect(jobs_, &QListWidget::currentItemChanged, this, updateActions);
    updateActions();
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::fillLibrary);
    connect(applicationFilter_, &QComboBox::currentTextChanged, this, &MainWindow::fillLibrary);
    connect(clips_, &QTableWidget::itemSelectionChanged, this, &MainWindow::selectClip);
    connect(clips_, &QTableWidget::cellDoubleClicked, this, [this] {
        if (!selectedPath_.isEmpty())
            showPlayer(selectedPath_);
    });
    return page;
}
QWidget *MainWindow::recordingPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    auto *top = new QHBoxLayout;
    button("Start / Stop Recording", top, [this] { toggleRecording(); });
    button("Open Toolbar", top, [this] { toolbar_->open(); });
    button("Test Encoders", top, [this] { probeEncoders(); });
    button("Calibrate Capture", top, [this] { calibrate(); });
    button("Refresh sources", top, [this] { refreshCaptureSources(); });
    v->addLayout(top);
    recordStats_ = new QLabel;
    recordStats_->setWordWrap(true);
    recordStats_->setFont(mono());
    v->addWidget(recordStats_);
    auto *hint = new QLabel("Recording settings control new footage. Changes while recording require "
                            "approval and clear the replay buffer.");
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto *form = new QFormLayout;
    source_ = new QComboBox;
    source_->addItem("Display", "monitor");
    source_->addItem("Window", "window");
    selectValue(source_, settings_.source);
    display_ = new QComboBox;
    populateDisplays(display_, registry_.displays, settings_.monitorIdentity, settings_.monitor);
    windows_ = new QComboBox;
    backend_ = new QComboBox;
    backend_->addItem("Windows Graphics Capture (DXGI monitor fallback)", "wgc");
    backend_->addItem("DXGI monitor capture · SDR only", "dxgi");
    selectValue(backend_, settings_.backend);
    encoder_ = new QComboBox;
    encoder_->setFont(mono());
    encoder_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    encoder_->setMinimumContentsLength(28);
    recordQuality_ = new QComboBox;
    recordQuality_->addItems({"Maximum", "High", "Balanced", "Compact"});
    recordQuality_->setCurrentText(settings_.quality);
    recordFps_ = new QComboBox;
    for (int f : {15, 30, 60, 120})
        recordFps_->addItem(QString::number(f) + " fps", f);
    selectValue(recordFps_, settings_.fps);
    recordHeight_ = new QComboBox;
    recordHeight_->addItem("Source resolution", 0);
    for (int h : {2160, 1440, 1080, 900, 720, 540, 480})
        recordHeight_->addItem(QString::number(h) + "p", h);
    selectValue(recordHeight_, settings_.height);
    recordContainer_ = new QComboBox;
    for (auto c : {"mp4", "mkv", "webm", "mov"})
        recordContainer_->addItem(QString(c).toUpper(), c);
    selectValue(recordContainer_, settings_.container);
    hdr_ = new QCheckBox("Preserve HDR (requires a verified 10-bit path)");
    hdr_->setChecked(settings_.hdr);
    form->addRow("Capture", source_);
    form->addRow("Display", display_);
    form->addRow("Window", windows_);
    form->addRow("Capture backend", backend_);
    form->addRow("Encoder", encoder_);
    form->addRow("Recording quality", recordQuality_);
    form->addRow("Recording frame rate", recordFps_);
    form->addRow("Recording resolution", recordHeight_);
    form->addRow("Default export container", recordContainer_);
    form->addRow("Colour", hdr_);
    v->addLayout(form);
    encoderDetail_ = new QLabel;
    encoderDetail_->setWordWrap(true);
    encoderDetail_->setFont(mono());
    v->addWidget(encoderDetail_);
    connect(encoder_, &QComboBox::currentIndexChanged, this, [this] {
        auto *c = registry_.find(encoder_->currentData().toString());
        encoderDetail_->setText(c ? c->backend + " · " + c->reason +
                                        "\nPixel formats: " + c->pixelFormats.join(", ") +
                                        "\nControls: " + c->rateControls.join(", ")
                                  : "Automatic selection requires successful capture calibration.");
    });
    auto *audioLabel = new QLabel("Audio sources · first track is the compatibility mix. Device selections "
                                  "remain fixed; system and application loopback cannot overlap.");
    audioLabel->setWordWrap(true);
    v->addWidget(audioLabel);
    audio_ = new QTableWidget(0, 7);
    audio_->setHorizontalHeaderLabels(
        {"Enabled", "Source", "Gain", "Mute", "Mix", "Separate", "Push-to-talk"});
    audio_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    v->addWidget(audio_);
    auto *audioActions = new QHBoxLayout;
    button("Add audio source", audioActions, [this] {
        auto devices = AudioEngine::devices();
        QStringList choices;
        for (auto &d : devices)
            choices << (d.input ? "Microphone · " : "System output · ") + d.name;
        choices << "Per-application audio";
        bool ok = false;
        auto choice = QInputDialog::getItem(this, "Audio source", "Choose a source", choices, 0, false, &ok);
        if (!ok)
            return;
        int index = choices.indexOf(choice);
        AudioSource a;
        if (index == devices.size()) {
            if (!AudioEngine::processCaptureSupported()) {
                showError("Per-application capture requires Windows build 20348 or later. Choose a labelled "
                          "system output device on this Windows version.");
                return;
            }
            bool accepted = false;
            int pid =
                QInputDialog::getInt(this, "Application audio", "Process ID", 0, 1, INT_MAX, 1, &accepted);
            if (!accepted)
                return;
            a.kind = "application";
            a.pid = quint32(pid);
            a.id = "process:" + QString::number(pid);
            a.name = "Process " + QString::number(pid);
            auto identity = processIdentity(a.pid);
            if (identity.isEmpty()) {
                showError("This application process is unavailable or cannot be identified");
                return;
            }
            a.processCreated = identity["created"].toString();
            a.processExecutable = identity["executable"].toString();
            a.name = QFileInfo(a.processExecutable).completeBaseName() + " · " + QString::number(a.pid);
        } else {
            auto d = devices[index];
            a.id = d.id;
            a.name = d.name;
            a.kind = d.input ? "microphone" : "system";
        }
        settings_.audio = readRecording().audio;
        settings_.audio << a;
        refreshSources();
    });
    button("Remove selected source", audioActions, [this] {
        int row = audio_->currentRow();
        if (row >= 0 && row < settings_.audio.size()) {
            settings_.audio = readRecording().audio;
            settings_.audio.removeAt(row);
            refreshSources();
        }
    });
    v->addLayout(audioActions);
    button("Apply Recording Settings", v, [this] { applyRecording(); });
    v->addStretch();
    return scrollPage(page);
}
QWidget *MainWindow::settingsPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    auto *form = new QFormLayout;
    libraryRoot_ = new QLineEdit(settings_.library);
    bufferRoot_ = new QLineEdit(settings_.bufferFolder);
    retention_ = new QSpinBox;
    retention_->setRange(10, 3600);
    retention_->setSuffix(" seconds");
    retention_->setValue(settings_.retention);
    storage_ = new QSpinBox;
    storage_->setRange(256, 65536);
    storage_->setSuffix(" MiB");
    storage_->setValue(int(settings_.bufferBytes / 1048576));
    for (auto *b : {retention_, storage_})
        b->setFont(mono());
    form->addRow("Clip library folder", libraryRoot_);
    form->addRow("Replay storage folder", bufferRoot_);
    form->addRow("Requested retention", retention_);
    form->addRow("Maximum replay storage", storage_);
    v->addLayout(form);
    auto *folders = new QHBoxLayout;
    button("Choose library folder", folders, [this] {
        auto d = QFileDialog::getExistingDirectory(this, "Clip library folder", libraryRoot_->text());
        if (!d.isEmpty())
            libraryRoot_->setText(d);
    });
    button("Choose replay folder", folders, [this] {
        auto d = QFileDialog::getExistingDirectory(this, "Replay storage folder", bufferRoot_->text());
        if (!d.isEmpty())
            bufferRoot_->setText(d);
    });
    v->addLayout(folders);
    button("Apply Folder and Buffer Settings", v, [this] { applyRecording(); });
    auto *shortcuts = new QGroupBox("Global shortcuts");
    auto *shortcutForm = new QFormLayout(shortcuts);
    auto binding = [&](const QString &label, const QString &command, const QString &initial) {
        auto *row = new QWidget;
        auto *column = new QVBoxLayout(row);
        column->setContentsMargins(0, 0, 0, 0);
        auto *l = new QHBoxLayout;
        l->setContentsMargins(0, 0, 0, 0);
        auto *edit = new ShortcutEdit(QKeySequence(initial), [this](bool capturing) {
            if (exiting_)
                return;
            QString error;
            if (!hotkeys_.setSuspended(capturing, &error))
                status_->setText(error);
        });
        edit->setObjectName(command + "Shortcut");
        edit->setAccessibleName(label + " shortcut");
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        edit->setFont(mono());
        l->addWidget(edit);
        auto *feedback = new QLabel("Active: " + QKeySequence(initial).toString(QKeySequence::NativeText));
        feedback->setObjectName(command + "ShortcutStatus");
        feedback->setWordWrap(true);
        auto *apply = button("Apply", l, [this, edit, command, feedback] {
            QString text = edit->keySequence().toString(QKeySequence::PortableText);
            auto previous = settings_;
            auto fail = [this, feedback](const QString &reason) {
                feedback->setText(reason);
                feedback->setStyleSheet("color:#f39290");
                status_->setText(reason);
            };
            QString error;
            if (command == "ptt") {
                UINT mods = 0, key = 0;
                if (!Hotkeys::parse(text, mods, key)) {
                    fail("Choose one supported keyboard shortcut.");
                    return;
                }
                if (recorder_.active()) {
                    fail("Stop recording before changing push-to-talk. The existing binding remains active.");
                    return;
                }
                settings_.pttHotkey = text;
                for (auto &a : settings_.audio) {
                    a.pushKey = int(key);
                    a.pushModifiers = int(mods);
                }
            } else {
                if (!hotkeys_.assign(command, text, &error)) {
                    fail(error);
                    return;
                }
                if (command == "toolbar")
                    settings_.overlayHotkey = text;
                else
                    settings_.recordHotkey = text;
            }
            if (!settings_.save()) {
                settings_ = previous;
                if (command != "ptt") {
                    const auto original = command == "toolbar" ? previous.overlayHotkey : previous.recordHotkey;
                    QString rollback;
                    if (!hotkeys_.assign(command, original, &rollback))
                        error = " " + rollback;
                }
                fail("Could not save the shortcut. Previous settings retained." + error);
            } else {
                feedback->setText("Active: " + edit->keySequence().toString(QKeySequence::NativeText));
                feedback->setStyleSheet("color:#b2edc8");
                status_->setText("Shortcut applied");
            }
        });
        apply->setObjectName(command + "ShortcutApply");
        apply->setAccessibleName("Apply " + label.toLower() + " shortcut");
        column->addLayout(l);
        column->addWidget(feedback);
        shortcutForm->addRow(label, row);
    };
    binding("Clipping toolbar", "toolbar", settings_.overlayHotkey);
    binding("Start / stop recording", "record", settings_.recordHotkey);
    binding("Microphone push-to-talk", "ptt", settings_.pttHotkey);
    auto *shortcutHint = new QLabel("Focus a shortcut and press your keys, then Tab to Apply. "
                                   "Changes take effect after Apply; leaving the app restores the active shortcuts.");
    shortcutHint->setWordWrap(true);
    shortcutForm->addRow(shortcutHint);
    v->addWidget(shortcuts);
    auto *startup = new QCheckBox("Open SippingCloftware when I sign in to Windows");
    startup->setChecked(settings_.startup);
    auto *record = new QCheckBox("Start recording automatically when launched");
    record->setChecked(settings_.autoRecord);
    auto *notifications = new QCheckBox("Show Windows notifications");
    notifications->setChecked(settings_.notifications);
    auto *border = new QCheckBox("Prefer borderless capture for SDR displays");
    border->setChecked(settings_.borderlessDisplay);
    border->setToolTip("Applies when recording next starts. Windows may show a border for window or HDR capture.");
    v->addWidget(startup);
    v->addWidget(record);
    v->addWidget(border);
    v->addWidget(notifications);
    connect(startup, &QCheckBox::clicked, this, [this, startup](bool enabled) {
        QString error;
        if (registerStartup(enabled, &error)) {
            settings_.startup = enabled;
            settings_.save();
        } else {
            startup->setChecked(!enabled);
            showError(error);
        }
    });
    connect(record, &QCheckBox::clicked, this, [this](bool enabled) {
        settings_.autoRecord = enabled;
        settings_.save();
    });
    connect(border, &QCheckBox::clicked, this, [this](bool enabled) {
        settings_.borderlessDisplay = enabled;
        settings_.save();
        status_->setText("Border preference applies when recording next starts");
    });
    connect(notifications, &QCheckBox::clicked, this, [this](bool enabled) {
        settings_.notifications = enabled;
        settings_.save();
    });
    auto *overrides = new QListWidget;
    auto showOverrides = [this, overrides] {
        overrides->clear();
        for (auto i = settings_.folderOverrides.begin(); i != settings_.folderOverrides.end(); ++i) {
            auto *item = new QListWidgetItem(i.key() + " → " + i.value(), overrides);
            item->setData(Qt::UserRole, i.key());
        }
    };
    showOverrides();
    v->addWidget(new QLabel("Application folder overrides (executable identity)"));
    v->addWidget(overrides);
    auto *overrideActions = new QHBoxLayout;
    button("Add override", overrideActions, [this, showOverrides] {
        auto exe =
            QFileDialog::getOpenFileName(this, "Choose application executable", {}, "Executables (*.exe)");
        if (exe.isEmpty())
            return;
        auto folder = QFileDialog::getExistingDirectory(this, "Choose this application's clip folder",
                                                        settings_.library);
        if (folder.isEmpty())
            return;
        settings_.folderOverrides[QDir::toNativeSeparators(exe)] = folder;
        settings_.save();
        showOverrides();
    });
    button("Remove override", overrideActions, [this, overrides, showOverrides] {
        if (auto *item = overrides->currentItem()) {
            settings_.folderOverrides.remove(item->data(Qt::UserRole).toString());
            settings_.save();
            showOverrides();
        }
    });
    v->addLayout(overrideActions);
    v->addStretch();
    return scrollPage(page);
}
void MainWindow::showError(const QString &error) {
    if (status_)
        status_->setText(error);
    QMessageBox::warning(this, "Action could not be completed", error);
}
void MainWindow::populateEncoders() {
    QString selected = encoder_->currentData().toString();
    if (selected.isEmpty())
        selected = settings_.encoder;
    encoder_->clear();
    encoder_->addItem("Automatic · calibrated H.264", "auto");
    for (auto &c : registry_.encoders)
        addChoice(encoder_, c.name + " · " + c.codec + " · " + (c.encoded ? "tested" : c.reason), c.name,
                  c.encoded && c.replaySuitable(),
                  c.replaySuitable() ? c.reason : "Export only: unsuitable replay stream");
    selectValue(encoder_, selected);
    updateCaptureChoices();
}
void MainWindow::updateCaptureChoices() {
    if (!hdr_)
        return;
    int index = display_->currentData().toInt();
    const bool window = source_->currentData().toString() == "window";
    if (window) {
        auto monitor =
            MonitorFromWindow(HWND(quintptr(windows_->currentData().toULongLong())), MONITOR_DEFAULTTONULL);
        for (int i = 0; i < registry_.displays.size(); ++i)
            if (registry_.displays[i].handle == quintptr(monitor)) {
                index = i;
                selectValue(display_, i);
                break;
            }
    }
    display_->setEnabled(!window);
    if (index < 0 || index >= registry_.displays.size())
        return;
    const auto &display = registry_.displays[index];
    bool verifiedHdr = false;
    for (const auto &encoder : registry_.encoders)
        verifiedHdr |= encoder.hdr;
    hdr_->setEnabled(display.hdr && verifiedHdr);
    hdr_->setToolTip(!display.hdr   ? "Selected display is currently SDR"
                     : !verifiedHdr ? "No encoder has passed a high-bit-depth encode/decode test"
                                    : "Preserves float capture through 10-bit BT.2020/PQ encoding");
    if (auto *model = qobject_cast<QStandardItemModel *>(backend_->model())) {
        auto *item = model->item(backend_->findData("dxgi"));
        if (item) {
            item->setEnabled(!window && !display.hdr);
            item->setToolTip(window        ? "DXGI captures displays only"
                             : display.hdr ? "HDR displays require floating-point WGC"
                                           : "SDR monitor fallback");
        }
    }
    if (display.refresh > 0 && recordFps_->findData(int(display.refresh)) < 0)
        recordFps_->addItem(QString("Source · %1 fps").arg(display.refresh), int(display.refresh));
    if (auto *model = qobject_cast<QStandardItemModel *>(recordFps_->model()))
        for (int row = 0; row < recordFps_->count(); ++row) {
            bool enabled = recordFps_->itemData(row).toInt() <= display.refresh;
            model->item(row)->setEnabled(enabled);
            model->item(row)->setToolTip(enabled ? QString()
                                                 : "This frame rate exceeds the selected source rate");
        }
    if (auto *model = qobject_cast<QStandardItemModel *>(recordHeight_->model()))
        for (int row = 0; row < recordHeight_->count(); ++row) {
            bool enabled = recordHeight_->itemData(row).toInt() <= display.height;
            model->item(row)->setEnabled(enabled);
            model->item(row)->setToolTip(enabled ? QString()
                                                 : "This resolution exceeds the selected display source");
        }
}
bool MainWindow::approveSdrRecording(Settings &s) {
    if (s.monitor < 0 || s.monitor >= registry_.displays.size())
        return true;
    if (registry_.displays[s.monitor].hdr && !s.hdr && !s.toneMapCapture) {
        if (QMessageBox::question(this, "Record an SDR conversion?",
                                  "The selected display is HDR. SDR recording requires tone mapping. Approve "
                                  "SDR recording, or choose No and enable Preserve HDR?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return false;
        s.toneMapCapture = true;
        settings_.toneMapCapture = true;
        settings_.save();
    }
    return true;
}
void MainWindow::refreshSources() {
    auto previous = windows_->currentData();
    auto previousIdentity = windows_->currentData(Qt::UserRole + 1).toJsonObject();
    QSignalBlocker blocker(windows_);
    windows_->clear();
    addChoice(
        windows_,
        settings_.windowExecutable.isEmpty()
            ? "Select a capture window"
            : "Reselect window · " + QFileInfo(settings_.windowExecutable).completeBaseName(),
        qulonglong(0), false,
        "Window handles are not reused after restarting the app; choose the intended window explicitly.");
    for (auto pair : captureWindows()) {
        auto identity = windowApplication(pair.first);
        windows_->addItem(pair.second, QVariant::fromValue(qulonglong(pair.first)));
        QJsonObject value{{"executable", identity.executable},
                          {"pid", qint64(identity.pid)},
                          {"created", identity.processCreated}};
        windows_->setItemData(windows_->count() - 1, value, Qt::UserRole + 1);
        if (pair.first == previous.toULongLong() && previousIdentity == value)
            windows_->setCurrentIndex(windows_->count() - 1);
    }
    audio_->setRowCount(int(settings_.audio.size()));
    for (int row = 0; row < settings_.audio.size(); ++row) {
        const auto &a = settings_.audio[row];
        auto checkBox = [&](int col, bool checked) {
            auto *c = new QCheckBox;
            c->setChecked(checked);
            c->setAccessibleName(audio_->horizontalHeaderItem(col)->text() + " " + a.name);
            audio_->setCellWidget(row, col, c);
        };
        checkBox(0, a.enabled);
        auto *item = new QTableWidgetItem(a.kind + " · " + a.name);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        audio_->setItem(row, 1, item);
        auto *gain = new QDoubleSpinBox;
        gain->setRange(0, 4);
        gain->setSingleStep(.1);
        gain->setValue(a.gain);
        gain->setFont(mono());
        gain->setAccessibleName("Gain " + a.name);
        audio_->setCellWidget(row, 2, gain);
        checkBox(3, a.mute);
        checkBox(4, a.mix);
        checkBox(5, a.separate);
        checkBox(6, a.pushToTalk);
        audio_->cellWidget(row, 6)->setEnabled(a.kind == "microphone");
    }
    audio_->resizeRowsToContents();
}
void MainWindow::refreshCaptureSources() {
    const auto pending = readRecording();
    const auto oldKey = registry_.cacheKey();
    auto tested = registry_.encoders;
    auto recommended = registry_.recommended;
    registry_.discover();
    if (registry_.cacheKey() == oldKey) {
        registry_.encoders = tested;
        registry_.recommended = recommended;
    } else {
        if (recorder_.active())
            recorder_.stop();
        const bool retained = registry_.loadCache();
        status_->setText(retained
            ? "Display configuration changed. Capture stopped; encoder tests retained. Recalibrate for automatic capture. Completed footage retained."
            : "Hardware/display configuration changed. Capture stopped; rerun encoder tests and calibration. Completed footage retained.");
    }
    populateDisplays(display_, registry_.displays, pending.monitorIdentity, pending.monitor);
    settings_.audio = pending.audio;
    refreshSources();
    populateEncoders();
    updateCaptureChoices();
}
Settings MainWindow::readRecording() const {
    Settings s = settings_;
    s.source = source_->currentData().toString();
    s.monitor = display_->currentData().toInt();
    s.monitorIdentity = display_->currentData(Qt::UserRole + 1).toString();
    s.window = quintptr(windows_->currentData().toULongLong());
    if (s.source == "window" && s.window) {
        const auto identity = windows_->currentData(Qt::UserRole + 1).toJsonObject();
        s.windowExecutable = identity["executable"].toString();
        s.windowProcessCreated = identity["created"].toString();
        s.windowPid = quint32(identity["pid"].toInteger());
    }
    s.backend = backend_->currentData().toString();
    s.encoder = encoder_->currentData().toString();
    s.quality = recordQuality_->currentText();
    s.fps = recordFps_->currentData().toInt();
    s.height = recordHeight_->currentData().toInt();
    s.container = recordContainer_->currentData().toString();
    s.hdr = hdr_->isChecked();
    s.retention = retention_->value();
    s.bufferBytes = qint64(storage_->value()) * 1048576;
    s.library = libraryRoot_->text();
    s.bufferFolder = bufferRoot_->text();
    for (int i = 0; i < s.audio.size(); ++i) {
        auto &a = s.audio[i];
        a.enabled = qobject_cast<QCheckBox *>(audio_->cellWidget(i, 0))->isChecked();
        a.gain = qobject_cast<QDoubleSpinBox *>(audio_->cellWidget(i, 2))->value();
        a.mute = qobject_cast<QCheckBox *>(audio_->cellWidget(i, 3))->isChecked();
        a.mix = qobject_cast<QCheckBox *>(audio_->cellWidget(i, 4))->isChecked();
        a.separate = qobject_cast<QCheckBox *>(audio_->cellWidget(i, 5))->isChecked();
        a.pushToTalk = qobject_cast<QCheckBox *>(audio_->cellWidget(i, 6))->isChecked();
    }
    return s;
}
void MainWindow::applyRecording() {
    auto next = readRecording();
    try {
        AudioEngine::validateSources(next.audio);
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
        return;
    }
    bool wasActive = recorder_.active();
    auto resolved = next;
    if (resolved.encoder == "auto")
        resolved.encoder = recorded_.encoder;
    bool restart = wasActive && captureSettings(resolved) != captureSettings(recorded_);
    if (restart) {
        if (QMessageBox::question(
                this, "Restart capture?",
                "Applying these settings stops capture and clears the replay buffer. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        recorder_.stop();
    }
    settings_ = next;
    if (!settings_.save()) {
        showError("Recording settings could not be saved");
        return;
    }
    status_->setText("Recording settings applied");
    if (restart) {
        resetApproved_ = true;
        toggleRecording();
    }
}
void MainWindow::toggleRecording() {
    toolbar_->hide();
    if (recorder_.active()) {
        recorder_.stop();
        updateStatus();
        return;
    }
    startRecording(false);
}
QJsonObject MainWindow::runtimeStatus() {
    const auto stats = recorder_.stats();
    const auto profile = stats.active ? recorded_ : readRecording();
    QJsonArray jobs;
    const auto all = exports_.jobs();
    for (int i = qMax(0, int(all.size()) - 20); i < all.size(); ++i) {
        const auto &job = all[i];
        jobs.append(QJsonObject{{"id", job.id}, {"state", job.state}, {"terminal", job.terminal},
                                {"message", job.message}, {"original", job.original}, {"output", job.output}});
    }
    return {{"message", status_->text()}, {"stats", stats.json()},
            {"bufferSeconds", recorder_.buffer().available(recorder_.buffer().latest())},
            {"profile", QJsonObject{{"encoder", profile.encoder}, {"fps", profile.fps}, {"height", profile.height},
                                    {"source", profile.source}, {"monitor", profile.monitor},
                                    {"monitorIdentity", profile.monitorIdentity}, {"container", profile.container},
                                    {"quality", profile.quality}, {"hdr", profile.hdr}}},
            {"shortcuts", QJsonObject{{"toolbar", settings_.overlayHotkey}, {"record", settings_.recordHotkey},
                                      {"pushToTalk", settings_.pttHotkey}, {"editing", hotkeys_.suspended()}}},
            {"jobs", jobs}};
}
QString MainWindow::saveRecent(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 86400)
        throw std::runtime_error("Clip duration must be between zero and one day");
    if (!recorder_.active())
        throw std::runtime_error("Recording is not active. Start recording before saving a recent clip.");
    auto output = recorded_;
    output.library = settings_.library;
    output.folderOverrides = settings_.folderOverrides;
    output.clipMic = settings_.clipMic;
    output.clipDesktop = settings_.clipDesktop;
    const auto source = recorder_.buffer().pin(clockSeconds(), seconds);
    try {
        const auto id = exports_.submitSave(source, output);
        status_->setText("Clip queued with fixed endpoint · " + durationText(source.duration()));
        return id;
    } catch (...) {
        recorder_.buffer().release(source.id);
        throw;
    }
}
void MainWindow::startRecording(bool atLaunch) {
    if (recorder_.active())
        return;
    try {
        auto s = readRecording();
        if (s.monitor < 0 || s.monitor >= registry_.displays.size())
            throw std::runtime_error("Select an attached capture display");
        const auto display = registry_.displays[s.monitor];
        if (atLaunch && s.source == "window" && !s.window)
            throw std::runtime_error("Reselect the capture window to resume recording");
        if (atLaunch && display.hdr && !s.hdr && !s.toneMapCapture)
            throw std::runtime_error("Review HDR capture settings before recording this display");
        const bool approved = std::exchange(resetApproved_, false);
        if (!approveSdrRecording(s))
            return;
        bool basicProfile = false;
        if (s.encoder == "auto") {
            s.encoder = registry_.automatic(s.hdr);
            auto *candidate = registry_.find(s.encoder);
            bool matched = false;
            if (candidate)
                for (auto value : candidate->tested) {
                    const auto profile = value.toObject();
                    if (profile.contains("capture") &&
                        profile["sourceKey"].toString() == captureProfileKey(s) &&
                        profile["adapter"].toInt() == s.monitor && profile["fps"].toInt() == s.fps &&
                        profile["requestedHeight"].toInt() == s.height &&
                        profile["source"].toString() == s.source &&
                        profile["backend"].toString() == s.backend &&
                        profile["quality"].toString() == s.quality && profile["audio"] == s.json()["audio"] &&
                        profile["hdr"].toBool() == s.hdr)
                        matched = true;
                }
            if (!matched) {
                if (!atLaunch || s.hdr)
                    throw std::runtime_error(s.encoder.isEmpty()
                        ? "Run Test Encoders and Calibrate Capture before automatic recording, or choose a tested encoder."
                        : "The capture profile changed since calibration. Calibrate the selected settings before using automatic recording.");
                s.encoder = "libx264";
                for (auto &caps : registry_.encoders)
                    if (caps.name == s.encoder && !caps.encoded) {
                        const auto probe = Registry::testEncoder(s.encoder);
                        caps.initialized = probe["initialized"].toBool();
                        caps.encoded = probe["encoded"].toBool();
                        caps.tested.append(probe);
                        break;
                    }
                s.fps = qMin(s.fps, qMin(30, int(display.refresh)));
                s.height = qMin(s.height > 0 ? s.height : display.height, qMin(720, display.height));
                basicProfile = true;
            }
        }
        auto *c = registry_.find(s.encoder);
        if (!c || !c->encoded)
            throw std::runtime_error("Selected encoder has not passed its test");
        if (!c->containers.contains(s.container))
            throw std::runtime_error("Selected codec is incompatible with the chosen export container");
        if (s.height > display.height)
            throw std::runtime_error("Recording resolution cannot exceed the selected source");
        if (s.fps > display.refresh)
            throw std::runtime_error("Recording frame rate cannot exceed the source rate");
        if (!approved && recorder_.buffer().count() > 0 && captureSettings(s) != captureSettings(recorded_)) {
            if (QMessageBox::question(
                    this, "Reset replay buffer?",
                    "Starting with these recording settings clears the previous replay buffer. Continue?",
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        }
        settings_ = readRecording();
        if (!settings_.save())
            throw std::runtime_error("Cannot persist recording settings");
        recorder_.start(s, registry_);
        recorded_ = s;
        updateStatus();
        if (basicProfile)
            status_->setText("Recording with the basic startup profile. Calibrate to use your preferred profile.");
    } catch (const std::exception &e) {
        const auto message = QString::fromUtf8(e.what());
        if (atLaunch) {
            status_->setText("Recording did not start · " + message);
            if (settings_.notifications)
                tray_->showMessage(AppName, status_->text(), QSystemTrayIcon::Warning);
        } else
            showError(message);
    }
}
void MainWindow::probeEncoders() {
    if (discovery_) {
        showError("Hardware checks are already running");
        return;
    }
    if (recorder_.active()) {
        showError("Stop recording before testing encoders");
        return;
    }
    status_->setText("Testing encoders in isolated processes…");
    cancelDiscovery_ = false;
    auto registry = registry_;
    discovery_ = QThread::create([this, registry]() mutable {
        registry.probe(
            [this](const QString &name) {
                QMetaObject::invokeMethod(
                    this, [this, name] { status_->setText("Testing encoder · " + name); },
                    Qt::QueuedConnection);
            },
            &cancelDiscovery_, false);
        QMetaObject::invokeMethod(
            this,
            [this, registry] {
                registry_ = registry;
                populateEncoders();
                status_->setText(
                    "Encoder tests finished. Calibrate a capture profile before automatic recording.");
            },
            Qt::QueuedConnection);
    });
    connect(discovery_, &QThread::finished, this, [this] {
        discovery_->deleteLater();
        discovery_ = nullptr;
    });
    discovery_->start();
}
void MainWindow::calibrate() {
    if (discovery_ || recorder_.active()) {
        showError("Stop recording and wait for encoder tests before calibrating");
        return;
    }
    Settings selected = readRecording();
    if (!approveSdrRecording(selected))
        return;
    for (const auto &job : exports_.jobs())
        if (!job.terminal) {
            showError(
                "Wait for pending exports before capture calibration so the measurement is representative.");
            return;
        }
    QStringList choices;
    if (selected.encoder != "auto")
        choices << selected.encoder;
    else
        for (auto &c : registry_.encoders)
            if (c.encoded &&
                (selected.hdr ? (c.hdr && (c.codecId == AV_CODEC_ID_HEVC || c.codecId == AV_CODEC_ID_AV1 ||
                                           c.codecId == AV_CODEC_ID_H264))
                              : (c.codecId == AV_CODEC_ID_H264)) &&
                (c.hardware || c.name == "libx264" || c.name == "libx265" || c.name == "libsvtav1"))
                choices << c.name;
    if (choices.isEmpty()) {
        showError("Run encoder tests first");
        return;
    }
    status_->setText("Calibrating the selected capture profile…");
    if (!QDir().mkpath(selected.bufferFolder)) {
        showError("Cannot create the selected replay storage folder for calibration");
        return;
    }
    const auto sourceKey = captureProfileKey(selected);
    auto registry = registry_;
    cancelDiscovery_ = false;
    discovery_ = QThread::create([this, selected, choices, registry, sourceKey]() mutable {
        double best = 1e30;
        QString winner;
        QJsonArray results;
        for (auto name : choices) {
            if (cancelDiscovery_)
                break;
            Settings s = selected;
            s.encoder = name;
            QTemporaryDir tmp(QDir(selected.bufferFolder).filePath(".calibration-XXXXXX"));
            if (!tmp.isValid()) {
                results << QJsonObject{
                    {"encoder", name},
                    {"error", "Cannot create calibration storage on the selected replay drive"}};
                continue;
            }
            s.bufferFolder = tmp.path();
            Recorder recorder;
            QJsonObject result{{"encoder", name}};
            QElapsedTimer elapsed;
            elapsed.start();
            double cpuStart = processCpuSeconds();
            try {
                recorder.start(s, registry);
                for (int i = 0; i < 30 && !cancelDiscovery_; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                recorder.stop();
                auto stats = recorder.stats();
                result["stats"] = stats.json();
                const auto segments = recorder.buffer().segments();
                bool decoded = !segments.isEmpty();
                for (const auto &segment : segments)
                    if (!decodeValidate(segment.path, &cancelDiscovery_)) {
                        decoded = false;
                        break;
                    }
                result["captureSegmentsDecoded"] = decoded;
                bool verified = stats.error.isEmpty() &&
                                stats.encoded >= quint64(qMax(1., (stats.elapsed - .2) * stats.fps * .95)) &&
                                stats.dropped == 0 && stats.backlog < 4 && decoded;
                result["passed"] = verified;
                for (auto &c : registry.encoders)
                    if (c.name == name) {
                        c.live = verified;
                        if (verified)
                            c.tested.append(QJsonObject{{"capture", stats.backend},
                                                        {"sourceKey", sourceKey},
                                                        {"width", stats.width},
                                                        {"height", stats.height},
                                                        {"fps", stats.fps},
                                                        {"adapter", s.monitor},
                                                        {"requestedHeight", s.height},
                                                        {"source", s.source},
                                                        {"backend", s.backend},
                                                        {"quality", s.quality},
                                                        {"audio", s.json()["audio"]},
                                                        {"hdr", s.hdr},
                                                        {"dropped", qint64(stats.dropped)}});
                    }
                double score = stats.dropped * 10000. + stats.backlog * 1000. +
                               (processCpuSeconds() - cpuStart) * 1000. + elapsed.elapsed() * .1;
                if (verified && score < best) {
                    best = score;
                    winner = name;
                }
                recorder.buffer().clear();
            } catch (const std::exception &e) {
                result["error"] = QString::fromUtf8(e.what());
            }
            results << result;
        }
        QMetaObject::invokeMethod(
            this,
            [this, registry, results, winner] {
                registry_ = registry;
                registry_.recommended = winner;
                auto cache = registry_.json();
                cache["calibration"] = results;
                cache["recommended"] = winner;
                writeJson(stateDir() + "/encoders.json", cache);
                populateEncoders();
                status_->setText(
                    winner.isEmpty()
                        ? "Capture calibration failed. Review results and lower capture cost explicitly."
                        : "Calibrated capture recommendation · " + winner);
            },
            Qt::QueuedConnection);
    });
    connect(discovery_, &QThread::finished, this, [this] {
        discovery_->deleteLater();
        discovery_ = nullptr;
    });
    discovery_->start();
}
void MainWindow::refreshLibrary() {
    library_.refresh(settings_.library);
}
void MainWindow::fillLibrary() {
    QSignalBlocker block(applicationFilter_);
    QString filter = applicationFilter_->currentText();
    QSet<QString> apps;
    for (auto &r : library_.records())
        apps.insert(r.application);
    applicationFilter_->clear();
    applicationFilter_->addItem("All applications");
    QStringList sorted = apps.values();
    sorted.sort();
    applicationFilter_->addItems(sorted);
    if (applicationFilter_->findText(filter) >= 0)
        applicationFilter_->setCurrentText(filter);
    else
        filter = "All applications";
    clips_->setRowCount(0);
    for (auto &r : library_.records()) {
        if (filter != "All applications" && r.application != filter)
            continue;
        if (!(r.path + ' ' + r.application).contains(search_->text(), Qt::CaseInsensitive))
            continue;
        int row = clips_->rowCount();
        clips_->insertRow(row);
        auto &m = r.media;
        QStringList values{
            QFileInfo(r.path).fileName(),
            r.application,
            QDateTime::fromString(r.created, Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm"),
            durationText(m["duration"].toDouble()),
            QString("%1×%2 · %3 fps\n%4 · %5")
                .arg(m["width"].toInt())
                .arg(m["height"].toInt())
                .arg(m["fps"].toDouble(), 0, 'f', 2)
                .arg(m["codec"].toString())
                .arg(QFileInfo(r.path).suffix().toUpper()),
            sizeText(m["bytes"].toInteger()),
            r.relationship};
        for (int c = 0; c < values.size(); ++c) {
            auto *item = new QTableWidgetItem(values[c]);
            if (c == 0)
                item->setData(Qt::UserRole, r.path);
            if (c >= 2 && c <= 5)
                item->setFont(mono());
            clips_->setItem(row, c, item);
        }
        clips_->setRowHeight(row, 62);
    }
    if (!selectedPath_.isEmpty())
        for (int i = 0; i < clips_->rowCount(); ++i)
            if (clips_->item(i, 0)->data(Qt::UserRole).toString() == selectedPath_)
                clips_->selectRow(i);
    for (int row = 0; row < qMin(clips_->rowCount(), 10); ++row)
        library_.requestThumbnail(clips_->item(row, 0)->data(Qt::UserRole).toString());
}
void MainWindow::selectClip() {
    int row = clips_->currentRow();
    if (row < 0 || !clips_->item(row, 0)) {
        selectedPath_.clear();
        preview_->setText("Select a clip to preview");
        details_->clear();
        return;
    }
    selectedPath_ = clips_->item(row, 0)->data(Qt::UserRole).toString();
    for (auto &r : library_.records())
        if (r.path == selectedPath_) {
            auto m = r.media;
            details_->setText(QString("%1\n%2 × %3 · %4 fps\n%5 · %6\n%7 audio tracks · %8\n%9%10")
                                  .arg(durationText(m["duration"].toDouble()))
                                  .arg(m["width"].toInt())
                                  .arg(m["height"].toInt())
                                  .arg(m["fps"].toDouble(), 0, 'f', 2)
                                  .arg(m["codec"].toString())
                                  .arg(QFileInfo(r.path).suffix().toUpper())
                                  .arg(m["audioTracks"].toInt())
                                  .arg(m["hdr"].toBool() ? "HDR" : "SDR")
                                  .arg(r.relationship)
                                  .arg(r.parent.isEmpty() ? QString() : "\nOriginal: " + r.parent));
            break;
        }
    preview_->setText("Loading preview…");
    library_.requestThumbnail(selectedPath_);
}
void MainWindow::updateJobs() {
    QString selected = jobs_->currentItem() ? jobs_->currentItem()->data(Qt::UserRole).toString() : QString();
    jobs_->clear();
    for (auto &j : exports_.jobs()) {
        auto *item = new QListWidgetItem(
            j.created + ' ' + j.state + " · " +
                (j.terminal ? QString() : QString::number(qRound(j.progress * 100)) + "% · ") + j.message,
            jobs_);
        item->setData(Qt::UserRole, j.id);
        item->setToolTip(j.message);
        if (j.id == selected)
            jobs_->setCurrentItem(item);
    }
}
void MainWindow::updateStatus() {
    auto s = recorder_.stats();
    if (!s.active && libraryRefreshPending_) {
        libraryRefreshPending_ = false;
        refreshLibrary();
    }
    recordStats_->setText(
        QString("%1 · %2\n%3 × %4 · %5 fps · %6\nBuffer: %7 / %8 · dropped %9 · backlog %10 frames\nAudio "
                "discontinuities: %11%12")
            .arg(s.active ? "Recording" : "Recording off")
            .arg(s.backend)
            .arg(s.width)
            .arg(s.height)
            .arg(s.fps)
            .arg(s.encoder)
            .arg(durationText(s.started ? recorder_.buffer().available(s.active ? clockSeconds()
                                                                                : recorder_.buffer().latest())
                                        : 0))
            .arg(sizeText(s.bytes))
            .arg(s.dropped)
            .arg(s.backlog)
            .arg(s.audioDiscontinuities)
            .arg(s.error.isEmpty() ? QString() : "\n" + s.error));
    if (!s.audioNotice.isEmpty())
        recordStats_->setText(recordStats_->text() + '\n' + s.audioNotice);
    recordStats_->setStyleSheet(s.error.isEmpty() ? (s.active ? "color:#87d7aa" : "color:#c7ccd5")
                                                  : "color:#ff9b9b");
    recordAction_->setText(s.active ? "Stop recording" : "Start recording");
    tray_->setToolTip(QString(AppName) + (s.active ? " · Recording " + durationText(s.elapsed) : " · Recording off"));
}
void MainWindow::showPlayer(const QString &path) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Playback");
    dialog->resize(960, 650);
    auto *v = new QVBoxLayout(dialog);
    auto *video = new QVideoWidget;
    v->addWidget(video, 1);
    auto *player = new QMediaPlayer(dialog);
    auto *audio = new QAudioOutput(dialog);
    player->setAudioOutput(audio);
    player->setVideoOutput(video);
    auto *controls = new QHBoxLayout;
    button("Play / Pause", controls, [player] {
        if (player->playbackState() == QMediaPlayer::PlayingState)
            player->pause();
        else
            player->play();
    });
    auto *slider = new QSlider(Qt::Horizontal);
    controls->addWidget(slider, 1);
    auto *time = new QLabel;
    time->setFont(mono());
    controls->addWidget(time);
    v->addLayout(controls);
    connect(player, &QMediaPlayer::durationChanged, slider,
            [slider](qint64 d) { slider->setRange(0, int(qMin(d, qint64(INT_MAX)))); });
    connect(player, &QMediaPlayer::positionChanged, dialog, [slider, time](qint64 p) {
        if (!slider->isSliderDown())
            slider->setValue(int(p));
        time->setText(durationText(double(p) / 1000.));
    });
    connect(slider, &QSlider::sliderMoved, player, &QMediaPlayer::setPosition);
    connect(player, &QMediaPlayer::errorOccurred, dialog, [this, player](auto, const QString &reason) {
        showError("Playback failed: " + reason);
        player->stop();
    });
    connect(dialog, &QDialog::finished, player, [player] {
        player->stop();
        player->setSource(QUrl());
    });
    player->setSource(QUrl::fromLocalFile(path));
    dialog->show();
    player->play();
}
void MainWindow::openClip(const QString &path) {
    showNormal();
    if (!backgroundUi()) {
        raise();
        activateWindow();
    }
    tabs_->setCurrentIndex(0);
    selectedPath_ = path;
    refreshLibrary();
}
void MainWindow::handleNotificationAction(const QString &arguments) {
    auto action = parseNotificationAction(arguments);
    if (!action.valid) {
        status_->setText("Invalid notification action rejected");
        return;
    }
    try {
        ExportJob job;
        for (const auto &candidate : exports_.jobs())
            if (candidate.id == action.job)
                job = candidate;
        if (job.id.isEmpty())
            throw std::runtime_error("This notification refers to an unavailable job");
        QString error;
        if (action.action == "retry") {
            if (!exports_.retryCopy(job.id, &error))
                throw std::runtime_error(error.toStdString());
        } else if (action.action == "yes" || action.action == "no") {
            if (!exports_.decideOriginal(job.id, action.action == "yes", &error))
                throw std::runtime_error(error.toStdString());
            if (!recorder_.active())
                refreshLibrary();
        } else {
            QString path = action.action != "open-original" && !job.output.isEmpty() ? job.output : job.original;
            auto identity = path == job.original ? job.details["originalIdentity"].toObject()
                                                 : job.details["outputIdentity"].toObject();
            if (identity.isEmpty() || fileIdentity(path) != identity)
                throw std::runtime_error("The clip from this notification moved, changed or is unavailable");
            openClip(path);
        }
    } catch (const std::exception &e) {
        status_->setText(QString::fromUtf8(e.what()));
        // Activation-only failures stay available in the app without opening the main window.
        qWarning("Notification action: %s", e.what());
    }
}
void MainWindow::showEditor(int tab, bool sdr) {
    if (selectedPath_.isEmpty())
        return;
    try {
        auto info = inspect(selectedPath_);
        if (sdr && !info.hdr)
            throw std::runtime_error("This clip is already SDR; no tone-mapped copy is needed");
        auto encoder =
            sdr ? registry_.forCodec(AV_CODEC_ID_H264) : registry_.forCodec(info.codecId, {}, info.hdr);
        if (encoder.isEmpty())
            throw std::runtime_error(
                "Run encoder tests to select a verified implementation of this clip's codec");
        auto *dialog = new EditorDialog(selectedPath_, encoder, tab, this);
        dialog->setSdrCopy(sdr);
        const auto original = selectedPath_;
        connect(dialog, &EditorDialog::exportRequested, this,
                [this, original, encoder](const EditRequest &request) {
                    try {
                        auto chosen = request.sdr ? registry_.forCodec(AV_CODEC_ID_H264) : encoder;
                        if (chosen.isEmpty())
                            throw std::runtime_error(
                                "No verified H.264 encoder is available for SDR sharing");
                        exports_.submitEdit(original, request, chosen, "High", settings_.library);
                    } catch (const std::exception &e) {
                        showError(QString::fromUtf8(e.what()));
                    }
                });
        dialog->show();
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (exiting_)
        event->accept();
    else if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        event->accept();
        exitApplication();
    } else {
        hide();
        event->ignore();
    }
}
void MainWindow::showControls() {
    toolbar_->hide();
    showNormal();
    const auto handle = HWND(winId());
    // A process launched with SW_HIDE can have its first native show suppressed.
    if (!IsWindowVisible(handle))
        ShowWindow(handle, backgroundUi() ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!backgroundUi()) {
        raise();
        activateWindow();
    }
}
void MainWindow::exitApplication() {
    exiting_ = true;
    cancelDiscovery_ = true;
    toolbar_->hide();
    recorder_.stop();
    exports_.shutdown();
    tray_->hide();
    QCoreApplication::quit();
}
class ClipChoiceStack : public QWidget {
    QComboBox *model_;
    QPushButton *summary_;
    QVBoxLayout *rows_;
    QWidget *highlight_;
    QPropertyAnimation *slide_;
    QList<QPushButton *> buttons_;
    QList<int> indices_;
    static constexpr int RowHeight = 34;

    void sync(bool animate) {
        slide_->stop();
        const int row = int(indices_.indexOf(model_->currentIndex()));
        for (int i = 0; i < buttons_.size(); ++i)
            buttons_[i]->setChecked(i == row);
        const auto selected = selectedLabel();
        summary_->setText(selected);
        summary_->setToolTip(model_->accessibleName() + " · " + selected);
        summary_->setAccessibleName(model_->accessibleName() + " choices");
        const bool wasVisible = highlight_->isVisible();
        highlight_->setVisible(row >= 0);
        if (row < 0)
            return;
        const QRect target = buttons_[row]->geometry().adjusted(0, 2, 0, -2);
        BOOL transitions = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &transitions, 0);
        if (animate && transitions && wasVisible && isVisible()) {
            slide_->setStartValue(highlight_->geometry());
            slide_->setEndValue(target);
            slide_->start();
        } else
            highlight_->setGeometry(target);
    }

  public:
    ClipChoiceStack(QComboBox *model, QPushButton *summary, QWidget *parent)
        : QWidget(parent), model_(model), summary_(summary), rows_(new QVBoxLayout(this)), highlight_(new QWidget(this)),
          slide_(new QPropertyAnimation(highlight_, "geometry", this)) {
        setStyleSheet(R"(
            QWidget { background:transparent; }
            QWidget#clipSelection { background:#323a40; border-radius:6px; }
            QWidget#clipSelectionMark { background:#a0e1b9; border-radius:1px; }
            QPushButton { background:transparent; border:1px solid transparent; border-radius:6px;
                          padding:0 11px; color:#dce1e6; text-align:left; }
            QPushButton:hover { background:rgba(255,255,255,13); }
            QPushButton:pressed { background:rgba(255,255,255,23); }
            QPushButton:checked { color:#fff; }
            QPushButton:disabled { color:#777f88; }
            QPushButton:focus { border-color:#80949f; }
        )");
        highlight_->setObjectName("clipSelection");
        highlight_->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *mark = new QWidget(highlight_);
        mark->setObjectName("clipSelectionMark");
        mark->setGeometry(0, (RowHeight - 4 - 12) / 2, 2, 12);
        slide_->setDuration(155);
        slide_->setEasingCurve(QEasingCurve::OutCubic);
        rows_->setContentsMargins(0, 0, 0, 0);
        rows_->setSpacing(0);
        connect(model_, &QComboBox::currentIndexChanged, this, [this] { sync(true); });
    }

    void populate(const QList<QPair<QVariant, QString>> &choices) {
        slide_->stop();
        for (auto *button : buttons_)
            delete button;
        buttons_.clear();
        indices_.clear();
        auto *items = qobject_cast<QStandardItemModel *>(model_->model());
        for (const auto &[value, label] : choices) {
            const int index = model_->findData(value);
            if (index < 0 || indices_.contains(index))
                continue;
            auto *button = new QPushButton(label, this);
            button->setAccessibleName(model_->accessibleName() + ": " + label);
            button->setCheckable(true);
            button->setFixedHeight(RowHeight);
            button->setEnabled(!items || items->item(index)->isEnabled());
            button->setToolTip(model_->itemData(index, Qt::ToolTipRole).toString());
            rows_->addWidget(button);
            buttons_ << button;
            indices_ << index;
            connect(button, &QPushButton::clicked, this, [this, index] {
                if (model_->currentIndex() == index) {
                    sync(false);
                    return;
                }
                model_->setCurrentIndex(index);
            });
        }
        setFixedHeight(int(buttons_.size()) * RowHeight);
        rows_->activate();
        highlight_->lower();
        sync(false);
    }

    QString selectedLabel() const {
        const auto row = indices_.indexOf(model_->currentIndex());
        return row >= 0 ? buttons_[row]->text() : model_->currentText();
    }

    void focusSelection() {
        const auto row = indices_.indexOf(model_->currentIndex());
        if (row >= 0 && buttons_[row]->isEnabled())
            buttons_[row]->setFocus();
        else
            for (auto *button : buttons_)
                if (button->isEnabled()) {
                    button->setFocus();
                    break;
                }
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        rows_->activate();
        sync(false);
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        rows_->activate();
        sync(false);
    }
};

class HudComboBox : public QComboBox {
  public:
    std::function<void(HudComboBox *)> openChoices;
    using QComboBox::QComboBox;

    void showPopup() override {
        openChoices(this);
    }

    void choose(int index) {
        setCurrentIndex(index);
        emit activated(index);
    }
};

namespace {
bool animateWindowsUi() {
    BOOL transitions = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &transitions, 0);
    return transitions != FALSE;
}

void slideIn(QWidget *window, QPropertyAnimation *motion, int offset) {
    if (!animateWindowsUi())
        return;
    const QPoint target = window->pos();
    const int left = window->screen()->availableGeometry().left();
    const int travel = window->isWindow() ? qBound(0, window->geometry().left() - left, offset) : offset;
    motion->setStartValue(target - QPoint(travel, 0));
    motion->setEndValue(target);
    motion->start();
}

class HudOptionButton : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    void paintEvent(QPaintEvent *event) override {
        QPushButton::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(isChecked() ? QColor("#e3ecef") : QColor("#9aa5ae"), 1.25,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center(width() - 12, height() / 2.0);
        p.drawPolyline(QPolygonF{center + QPointF(-1.5, -3), center + QPointF(1.5, 0),
                                 center + QPointF(-1.5, 3)});
    }
};

QIcon hudIcon(QColor color, bool save, bool recording = false) {
    QPixmap image(32, 32);
    image.setDevicePixelRatio(2);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (save)
        p.drawPolygon(QPolygonF{QPointF(4, 2.5), QPointF(13, 8), QPointF(4, 13.5)});
    else if (recording)
        p.drawRoundedRect(QRectF(3, 3, 10, 10), 1.5, 1.5);
    else
        p.drawEllipse(QRectF(3, 3, 10, 10));
    return QIcon(image);
}

void addHudDivider(QVBoxLayout *layout) {
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet("background:rgba(226,235,243,30); border:none");
    layout->addWidget(line);
}
} // namespace

ClipToolbar::ClipToolbar(MainWindow *owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), owner_(owner),
      backdrop_(std::make_unique<HudBackdrop>()) {
    setWindowTitle("Clipping toolbar");
    setObjectName("clipToolbar");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFont(QFont("Segoe UI Variable Text", 10));
    setAttribute(Qt::WA_TranslucentBackground);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { hide(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && isVisible())
            hide();
    });
    setStyleSheet(R"(
        QWidget#clipToolbar, QWidget#clipRail, QWidget#clipOptions, QWidget#clipAdvanced { background:transparent; }
        QPushButton { background:transparent; border:1px solid transparent; border-radius:6px;
                      padding:0 4px; color:#ecf1f4; }
        QPushButton:hover { background:rgba(255,255,255,19); }
        QPushButton:pressed { background:rgba(255,255,255,29); }
        QPushButton:disabled { color:#838b92; }
        QPushButton:checked { background:#303940; }
        QPushButton:focus { border-color:#80949f; }
        QPushButton[quickChoice="true"] { text-align:left; padding:0 20px 0 8px; }
        QPushButton#compressToggle:checked { color:#b2edc8; background:#24382e; }
        QPushButton[audioToggle="true"] { color:#929da6; text-align:left; padding:0 8px; }
        QPushButton[audioToggle="true"]:checked { color:#dcebe4; background:#23332d; }
    )");
    dim_ = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus |
                                Qt::WindowTransparentForInput | Qt::WindowStaysOnTopHint);
    dim_->setWindowTitle("Clipping toolbar dimmer");
    dim_->setAttribute(Qt::WA_ShowWithoutActivating);
    dim_->setStyleSheet("background:#000");
    dim_->setWindowOpacity(.38);
    hudMotion_ = new QPropertyAnimation(this, "pos", this);
    panelMotion_ = new QPropertyAnimation;
    panelMotion_->setParent(this);
    panelMotion_->setPropertyName("pos");
    dimMotion_ = new QPropertyAnimation(dim_, "windowOpacity", this);
    for (auto *motion : {hudMotion_, panelMotion_, dimMotion_}) {
        motion->setDuration(160);
        motion->setEasingCurve(QEasingCurve::OutCubic);
    }
    connect(panelMotion_, &QPropertyAnimation::valueChanged, this, [this] { updateSurfaceMask(); });
    rail_ = new QWidget(this);
    rail_->setObjectName("clipRail");
    rail_->setFixedWidth(110);
    options_ = new QWidget(this);
    options_->setObjectName("clipOptions");
    options_->setStyleSheet(R"(
        QWidget#clipOptions { background:transparent; }
        QLabel { color:#aeb9c2; }
    )");
    options_->setFixedWidth(156);
    auto *optionLayout = new QVBoxLayout(options_);
    optionLayout->setContentsMargins(5, 7, 5, 5);
    optionLayout->setSpacing(4);
    optionTitle_ = new QLabel(options_);
    optionTitle_->setContentsMargins(8, 1, 0, 1);
    optionTitle_->setFont(QFont("Segoe UI Variable Text", 9));
    optionLayout->addWidget(optionTitle_);
    optionPages_ = new QStackedWidget(options_);
    optionPages_->setStyleSheet("QStackedWidget { background:transparent; }");
    optionLayout->addWidget(optionPages_);
    options_->hide();
    advanced_ = new QWidget(this);
    advanced_->setObjectName("clipAdvanced");
    advanced_->setStyleSheet(R"(
        QLabel { color:#ecf1f4; }
        QPushButton,QComboBox { background:rgba(48,54,60,200); border:1px solid #51585f; border-radius:5px; padding:5px; }
        QListView { background:transparent; border:none; outline:none; }
        QListView::item { min-height:26px; padding:0 8px; }
        QListView::item:selected { background:#323a40; color:#fff; }
    )");
    advancedPages_ = new QStackedLayout(advanced_);
    auto *advancedForm = new QWidget(advanced_);
    auto *advancedList = new QWidget(advanced_);
    advancedForm->setObjectName("clipAdvancedForm");
    advancedList->setObjectName("clipAdvancedList");
    advancedForm->setStyleSheet("QWidget#clipAdvancedForm { background:transparent; }");
    advancedList->setStyleSheet("QWidget#clipAdvancedList { background:transparent; }");
    advancedPages_->addWidget(advancedForm);
    advancedPages_->addWidget(advancedList);
    auto *a = new QFormLayout(advancedForm);
    auto *listLayout = new QVBoxLayout(advancedList);
    auto *back = new QPushButton("Back to clip options", advancedList);
    auto *list = new QListView(advancedList);
    list->setObjectName("hudAdvancedChoices");
    list->setAccessibleName("Advanced clip choices");
    listLayout->addWidget(back);
    listLayout->addWidget(list);
    const auto closeList = [this] {
        auto *combo = std::exchange(openCombo_, nullptr);
        advancedPages_->setCurrentIndex(0);
        if (combo) {
            combo->hidePopup();
            combo->setFocus();
        }
        return combo;
    };
    connect(back, &QPushButton::clicked, this, closeList);
    const auto selectItem = [this, closeList](const QModelIndex &index) {
        if (openCombo_ && index.isValid() && index.flags().testFlag(Qt::ItemIsEnabled))
            closeList()->choose(index.row());
    };
    connect(list, &QListView::clicked, this, selectItem);
    connect(list, &QListView::activated, this, selectItem);
    const auto makeCombo = [this, advancedForm, list] {
        auto *combo = new HudComboBox(advancedForm);
        combo->setFont(mono());
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(13);
        combo->openChoices = [this, list](HudComboBox *selected) {
            openCombo_ = selected;
            list->setModel(selected->model());
            list->setAccessibleName(selected->accessibleName() + " choices");
            list->setCurrentIndex(selected->model()->index(selected->currentIndex(), 0));
            advancedPages_->setCurrentIndex(1);
            list->setFocus();
        };
        return combo;
    };
    advanced_->hide();
    auto *v = new QVBoxLayout(rail_);
    v->setContentsMargins(5, 5, 5, 5);
    v->setSpacing(2);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(2);
    save_ = button({}, controls, [this] { submit(); });
    save_->setAccessibleName("Save Clip");
    save_->setIcon(hudIcon(QColor("#a0e1b9"), true));
    record_ = button({}, controls, [this] { owner_->toggleRecording(); });
    for (auto *control : {save_, record_}) {
        control->setIconSize(QSize(16, 16));
        control->setFixedSize(49, 32);
    }
    v->addLayout(controls);
    addHudDivider(v);
    auto selector = [&](const QString &label, QComboBox *&box, ClipChoiceStack *&stack) {
        box = makeCombo();
        box->setAccessibleName(label);
        a->addRow(label, box);
        auto *summary = new HudOptionButton(rail_);
        summary->setCheckable(true);
        summary->setProperty("quickChoice", true);
        summary->setFixedHeight(31);
        v->addWidget(summary);
        auto *choices = new ClipChoiceStack(box, summary, optionPages_);
        stack = choices;
        optionPages_->addWidget(choices);
        connect(summary, &QPushButton::clicked, this, [this, choices, summary, label] {
            showOptions(choices, summary, label);
        });
    };
    selector("Container", container_, formatStack_);
    selector("Frame rate", fps_, fpsStack_);
    selector("Resolution", resolution_, resolutionStack_);
    selector("Quality", quality_, qualityStack_);
    quality_->addItems({"Maximum", "High", "Balanced", "Compact"});
    for (int i = 0; i < quality_->count(); ++i)
        quality_->setItemData(i, quality_->itemText(i));
    selector("Duration", duration_, durationStack_);
    addHudDivider(v);
    compress_ = new QPushButton("Auto Compress", rail_);
    compress_->setObjectName("compressToggle");
    compress_->setAccessibleName("Auto Compress");
    compress_->setCheckable(true);
    compress_->setFixedHeight(32);
    compress_->setFont(QFont("Segoe UI Variable Text", 9));
    compress_->setToolTip("When armed, Save Clip also attempts a separate copy within 20 MB");
    v->addWidget(compress_);
    auto audioToggle = [&](const QString &name, const QString &description) {
        auto *toggle = new QPushButton(name, rail_);
        toggle->setProperty("audioToggle", true);
        toggle->setAccessibleName(name);
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setFixedHeight(29);
        toggle->setFont(QFont("Segoe UI Variable Text", 9));
        toggle->setToolTip(description + " · applies only to this saved clip");
        v->addWidget(toggle);
        return toggle;
    };
    mic_ = audioToggle("Mic", "Include buffered microphone audio");
    desktop_ = audioToggle("Desktop Audio", "Include buffered desktop and application audio");
    partial_ = new QLabel(rail_);
    partial_->setAlignment(Qt::AlignCenter);
    partial_->setFont(QFont("Segoe UI Variable Text", 9));
    partial_->setStyleSheet("color:#b4bec7");
    partial_->hide();
    v->addWidget(partial_);
    available_ = new QLabel(advancedForm);
    available_->setWordWrap(true);
    available_->setFont(mono());
    a->addRow(available_);
    encoder_ = makeCombo();
    encoder_->setAccessibleName("Codec / implementation");
    a->addRow("Codec / implementation", encoder_);
    auto *folder = new QPushButton("Show Folder", advancedForm);
    a->addRow(folder);
    connect(folder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(owner_->settings_.library)); });
    auto *utility = new QHBoxLayout;
    utility->setSpacing(2);
    auto *more = new QPushButton(QStringLiteral("···"), rail_);
    more->setFixedSize(49, 28);
    more->setAccessibleName("More clip options");
    more->setToolTip("More clip options");
    utility->addWidget(more);
    auto *close = new QPushButton(QStringLiteral("×"), rail_);
    close->setAccessibleName("Close toolbar");
    close->setToolTip("Close toolbar (Esc)");
    close->setFixedSize(49, 28);
    utility->addWidget(close);
    v->addLayout(utility);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    connect(more, &QPushButton::clicked, this, [this, more] {
        const bool open = advanced_->isHidden();
        hidePanel();
        if (!open)
            return;
        placePanel(advanced_, more);
        panelMotion_->setTargetObject(advanced_);
        slideIn(advanced_, panelMotion_, 6);
    });
    connect(duration_, &QComboBox::currentIndexChanged, this, [this] {
        int seconds = duration_->currentData().toInt();
        if (seconds < 0) {
            bool ok = false;
            seconds = QInputDialog::getInt(this, "Custom duration", "Seconds", 30, 1,
                                           qMax(1, recorded_.retention), 1, &ok);
            if (ok) {
                int index = duration_->findData(seconds);
                if (index < 0) {
                    duration_->insertItem(duration_->count() - 1, QString::number(seconds) + "s", seconds);
                    index = duration_->count() - 2;
                }
                duration_->setCurrentIndex(index);
            } else
                duration_->setCurrentIndex(1);
            return;
        }
        showDuration();
    });
    connect(container_, &QComboBox::currentIndexChanged, this, [this] {
        const auto choice = encoderForContainer(container_->currentData().toString());
        if (!choice.isEmpty())
            selectValue(encoder_, choice);
        const bool canSave = !pin_.id.isEmpty() && !choice.isEmpty();
        save_->setEnabled(canSave);
        compress_->setEnabled(canSave);
        showDuration();
    });
    connect(encoder_, &QComboBox::activated, this, [this] {
        const auto *cap = owner_->registry_.find(encoder_->currentData().toString());
        if (cap && !cap->containers.contains(container_->currentData().toString()))
            for (int i = 0; i < container_->count(); ++i)
                if (cap->containers.contains(container_->itemData(i).toString())) {
                    container_->setCurrentIndex(i);
                    break;
                }
    });
}
ClipToolbar::~ClipToolbar() {
    release();
    delete dim_;
}
void ClipToolbar::release() {
    if (!pin_.id.isEmpty()) {
        try {
            owner_->recorder_.buffer().release(pin_.id);
        } catch (const std::exception &e) {
            owner_->status_->setText(QString::fromUtf8(e.what()));
        }
        pin_ = {};
    }
}
void ClipToolbar::hideEvent(QHideEvent *event) {
    hudMotion_->stop();
    dimMotion_->stop();
    hidePanel();
    backdrop_->hide();
    dim_->hide();
    release();
    QWidget::hideEvent(event);
}
void ClipToolbar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(219, 231, 241, 63), 1));
    p.setBrush(acrylic_ ? QColor(17, 20, 23, 166) : QColor("#1c2024"));
    for (auto *surface : {rail_, options_, advanced_})
        if (!surface->isHidden())
            p.drawRoundedRect(QRectF(surface->geometry()).adjusted(.5, .5, -.5, -.5), 6, 6);
}
void ClipToolbar::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    updateBackdrop();
}
void ClipToolbar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
    } else
        QWidget::keyPressEvent(event);
}
QString ClipToolbar::encoderForContainer(const QString &container) const {
    const auto compatible = [&](const EncoderCaps &cap) {
        return cap.encoded && (!recorded_.hdr || cap.hdr) && cap.containers.contains(container);
    };
    if (const auto *cap = owner_->registry_.find(encoder_->currentData().toString()); cap && compatible(*cap))
        return cap->name;
    const auto preferred = recorded_.hdr ? AV_CODEC_ID_HEVC : container == "webm" ? AV_CODEC_ID_VP9 : AV_CODEC_ID_H264;
    for (const auto &cap : owner_->registry_.encoders)
        if (cap.codecId == preferred && !cap.hardware && compatible(cap))
            return cap.name;
    for (const auto &cap : owner_->registry_.encoders)
        if (compatible(cap))
            return cap.name;
    return {};
}
void ClipToolbar::placePanel(QWidget *panel, QWidget *anchor) {
    panel->adjustSize();
    const auto area = screen()->availableGeometry();
    const int availableHeight = area.bottom() - mapToGlobal(QPoint()).y() + 1;
    panel->move(rail_->width() + 7,
                qBound(0, anchor->mapTo(this, QPoint()).y(), qMax(0, availableHeight - panel->height())));
    panel->show();
    layoutSurfaces();
}
void ClipToolbar::layoutSurfaces() {
    rail_->adjustSize();
    QRect bounds = rail_->geometry();
    for (auto *panel : {options_, advanced_})
        if (!panel->isHidden())
            bounds = bounds.united(panel->geometry());
    setFixedSize(bounds.size());
    updateSurfaceMask();
}
void ClipToolbar::updateSurfaceMask() {
    QRegion region;
    for (auto *surface : {rail_, options_, advanced_})
        if (!surface->isHidden()) {
            QPainterPath path;
            path.addRoundedRect(QRectF(surface->geometry()), 6, 6);
            region += QRegion(path.toFillPolygon().toPolygon());
        }
    setMask(region);
    updateBackdrop();
    update();
}
void ClipToolbar::updateBackdrop() {
    if (!isVisible())
        return;
    QList<QRect> surfaces{rail_->geometry()};
    for (auto *panel : {options_, advanced_})
        if (!panel->isHidden())
            surfaces << panel->geometry();
    const bool visible = testAttribute(Qt::WA_TranslucentBackground) &&
                         backdrop_->update(HWND(winId()), surfaces, devicePixelRatioF());
    if (acrylic_ != visible) {
        acrylic_ = visible;
        update();
    }
}
void ClipToolbar::hidePanel() {
    panelMotion_->stop();
    options_->hide();
    advanced_->hide();
    if (auto *combo = std::exchange(openCombo_, nullptr))
        combo->hidePopup();
    advancedPages_->setCurrentIndex(0);
    if (openChoice_)
        openChoice_->setChecked(false);
    openChoice_ = nullptr;
    layoutSurfaces();
}
void ClipToolbar::showOptions(ClipChoiceStack *choices, QPushButton *anchor, const QString &title) {
    const bool close = options_->isVisible() && openChoice_ == anchor;
    if (close) {
        hidePanel();
        return;
    }
    if (openChoice_)
        openChoice_->setChecked(false);
    const bool reveal = !options_->isVisible();
    panelMotion_->stop();
    if (auto *combo = std::exchange(openCombo_, nullptr))
        combo->hidePopup();
    advancedPages_->setCurrentIndex(0);
    advanced_->hide();
    optionTitle_->setText(title);
    optionPages_->setFixedHeight(choices->height());
    optionPages_->setCurrentWidget(choices);
    placePanel(options_, anchor);
    openChoice_ = anchor;
    anchor->setChecked(true);
    if (reveal) {
        panelMotion_->setTargetObject(options_);
        slideIn(options_, panelMotion_, 7);
    }
    choices->focusSelection();
}
void ClipToolbar::refreshChoices(int sourceHeight) {
    formatStack_->populate({{"mp4", "MP4"}, {"mkv", "MKV"}, {"webm", "WebM"}, {"mov", "MOV"}});
    QList<QPair<QVariant, QString>> rates;
    for (int fps : {recorded_.fps, 60, 30, 15})
        if (fps <= recorded_.fps)
            rates.append({fps, QString("%1 fps").arg(fps)});
    fpsStack_->populate(rates);
    QList<QPair<QVariant, QString>> resolutions{
        {0, sourceHeight > 0 ? QString("%1p").arg(sourceHeight) : QString("Source")}};
    for (int height : {1080, 720, 480})
        if (height < sourceHeight)
            resolutions.append({height, QString("%1p").arg(height)});
    resolutionStack_->populate(resolutions);
    qualityStack_->populate({{"Maximum", "Maximum"}, {"High", "High"}, {"Balanced", "Balanced"},
                            {"Compact", "Compact"}});
    durationStack_->populate({{300, "5m"}, {180, "3m"}, {60, "1m"}, {30, "30s"}, {10, "10s"}});
}
void ClipToolbar::showDuration() {
    const double buffered = pin_.duration();
    const int requested = duration_->currentData().toInt();
    const auto actual = durationText(qMin(double(requested), buffered));
    available_->setText(QString("Available: %1\nThis clip: %2").arg(durationText(buffered), actual));
    partial_->setText("Only " + actual);
    partial_->setVisible(save_->isEnabled() && buffered < requested);
    if (isVisible())
        layoutSurfaces();
    save_->setToolTip(pin_.id.isEmpty() ? "Start recording and buffer footage before saving"
                      : !save_->isEnabled() ? "No verified encoder for this export format"
                      : QString("Save %1 of %2 buffered, ending when the toolbar opened")
                            .arg(actual, durationText(buffered)));
}
void ClipToolbar::open() {
    if (isVisible()) {
        hide();
        return;
    }
    release();
    recorded_ = owner_->recorded_;
    auto stats = owner_->recorder_.stats();
    endpoint_ = stats.active ? clockSeconds() : owner_->recorder_.buffer().latest();
    if (stats.started > 0)
        try {
            pin_ = owner_->recorder_.buffer().pin(endpoint_, recorded_.retention);
        } catch (const std::exception &e) {
            owner_->status_->setText(QString::fromUtf8(e.what()));
        }
    const auto recordAction = stats.active ? "Stop Recording" : "Start Recording";
    record_->setIcon(hudIcon(stats.active ? QColor("#f39290") : QColor("#e2e9ef"), false, stats.active));
    record_->setAccessibleName(recordAction);
    record_->setToolTip(recordAction);
    fps_->clear();
    addChoice(fps_, QString("Source · %1 fps").arg(recorded_.fps), recorded_.fps);
    for (int f : {120, 60, 30, 15})
        if (f != recorded_.fps)
            addChoice(fps_, QString::number(f) + " fps", f, f <= recorded_.fps,
                      "Extra frames cannot be recovered from the replay buffer");
    resolution_->clear();
    addChoice(resolution_, QString("Source · %1 × %2").arg(stats.width).arg(stats.height), 0);
    for (int h : {2160, 1440, 1080, 900, 720, 540, 480})
        if (h != stats.height)
            addChoice(resolution_, QString::number(h) + "p", h, h < stats.height,
                      "Higher resolution cannot be recovered from buffered footage");
    {
        QSignalBlocker block(duration_);
        duration_->clear();
        for (auto pair : QList<QPair<QString, int>>{
                 {"10s", 10}, {"30s", 30}, {"1m", 60}, {"3m", 180}, {"5m", 300}, {"Custom…", -1}})
            duration_->addItem(pair.first, pair.second);
        duration_->setCurrentIndex(1);
    }
    quality_->setCurrentText(recorded_.quality);
    encoder_->clear();
    for (auto &cap : owner_->registry_.encoders)
        if (cap.encoded)
            addChoice(encoder_, cap.codec + " · " + cap.name, cap.name, !recorded_.hdr || cap.hdr,
                      "No verified high-bit-depth HDR output for this implementation");
    selectValue(encoder_, recorded_.encoder);
    {
        QSignalBlocker block(container_);
        container_->clear();
        for (auto name : {"mp4", "mkv", "webm", "mov"})
            addChoice(container_, QString(name).toUpper(), name, !encoderForContainer(name).isEmpty(),
                      "Requires a verified compatible export encoder");
        selectValue(container_, recorded_.container);
    }
    const auto compatibleEncoder = encoderForContainer(container_->currentData().toString());
    if (!compatibleEncoder.isEmpty())
        selectValue(encoder_, compatibleEncoder);
    save_->setEnabled(!pin_.id.isEmpty() && !compatibleEncoder.isEmpty());
    compress_->setEnabled(save_->isEnabled());
    compress_->setChecked(false);
    mic_->setChecked(true);
    desktop_->setChecked(true);
    showDuration();
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    const auto foreground = GetForegroundWindow();
    if (foreground && !requestedUiScreen()) {
        const HMONITOR active = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(active, &info))
            for (auto *candidate : QGuiApplication::screens())
                if (candidate->name().compare(QString::fromWCharArray(info.szDevice), Qt::CaseInsensitive) == 0) {
                    screen = candidate;
                    break;
                }
    }
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (auto *requested = requestedUiScreen())
        screen = requested;
    auto area = screen->availableGeometry();
    refreshChoices(stats.height);
    layoutSurfaces();
    move(area.left() + 12, area.top() + qMax(0, (area.height() - height()) / 2));
    auto handle = HWND(winId());
    bool excluded = excludeFromCapture(quintptr(handle));
    if (!excluded && testAttribute(Qt::WA_TranslucentBackground)) {
        // Some Windows 10 compositor configurations cannot exclude a per-pixel
        // layered window. Recreate the hidden HUD with the solid dark fallback
        // before displaying anything, then require capture exclusion again.
        backdrop_->hide();
        destroy(true, false);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);
        handle = HWND(winId());
        excluded = excludeFromCapture(quintptr(handle));
    }
    if (!excluded) {
        const auto reason = GetLastError();
        owner_->showError(QString("Windows could not exclude the toolbar from monitor capture (error %1)")
                              .arg(reason));
        release();
        return;
    }
    dim_->setGeometry(screen->geometry());
    if (excludeFromCapture(quintptr(dim_->winId()))) {
        const bool animate = animateWindowsUi();
        dim_->setWindowOpacity(animate ? .01 : .38);
        dim_->show();
        if (animate) {
            dimMotion_->setStartValue(.01);
            dimMotion_->setEndValue(.38);
            dimMotion_->start();
        }
    }
    show();
    SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!backgroundUi())
        activateWindow();
    if (dim_->isVisible())
        SetWindowPos(HWND(dim_->winId()), handle, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    updateBackdrop();
    slideIn(this, hudMotion_, 7);
    save_->setFocus();
}
void ClipToolbar::submit() {
    if (pin_.id.isEmpty())
        return;
    try {
        Settings output = recorded_;
        output.library = owner_->settings_.library;
        output.folderOverrides = owner_->settings_.folderOverrides;
        output.container = container_->currentData().toString();
        output.fps = fps_->currentData().toInt();
        output.height = resolution_->currentData().toInt();
        output.quality = quality_->currentText();
        output.encoder = encoder_->currentData().toString();
        output.clipMic = mic_->isChecked();
        output.clipDesktop = desktop_->isChecked();
        auto source = pin_;
        source.requested = duration_->currentData().toInt();
        source.start = qMax(source.start, source.end - source.requested);
        owner_->exports_.submitSave(source, output, compress_->isChecked());
        pin_ = {};
        hide();
        owner_->status_->setText("Clip queued with fixed endpoint · " + durationText(source.duration()));
    } catch (const std::exception &e) {
        owner_->showError(QString::fromUtf8(e.what()));
    }
}
} // namespace clip
