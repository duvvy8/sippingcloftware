#include "ui.h"
#include "editor.h"
#include <QStandardItemModel>
#include <cmath>
namespace clip {
namespace {
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
QJsonObject captureSettings(const Settings &s) {
    auto result = s.json();
    for (const auto &key : {"library", "container", "folders", "startup", "autoRecord", "notifications",
                            "overlayHotkey", "recordHotkey", "pttHotkey"})
        result.remove(key);
    result["window"] = qint64(s.window);
    return result;
}
} // namespace
void applyAppearance(QApplication &app) {
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
MainWindow::MainWindow() : exports_(&recorder_.buffer(), this), library_(this), hotkeys_(this) {
    settings_.load();
    recorded_ = settings_;
    registry_.discover();
    auto cache = readJson(stateDir() + "/encoders.json");
    if (cache["key"].toString() == registry_.cacheKey())
        registry_.probe();
    setWindowTitle("Clips");
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
    setCentralWidget(body);
    toolbar_ = new ClipToolbar(this);
    auto *menu = new QMenu(this);
    menu->addAction("Clips", this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction("Clipping toolbar", toolbar_, &ClipToolbar::open);
    menu->addAction("Start / stop recording", this, &MainWindow::toggleRecording);
    menu->addSeparator();
    menu->addAction("Exit", this, &MainWindow::exitApplication);
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    {
        QPainter p(&icon);
        p.setBrush(QColor(145, 152, 163));
        p.setPen(Qt::NoPen);
        p.drawEllipse(5, 5, 14, 14);
    }
    tray_ = new QSystemTrayIcon(QIcon(icon), this);
    tray_->setToolTip("Recording off");
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    tray_->show();
    QString error;
    if (!hotkeys_.assign("toolbar", settings_.overlayHotkey, &error) ||
        !hotkeys_.assign("record", settings_.recordHotkey, &error))
        QTimer::singleShot(0, this, [this, error] { showError(error); });
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
    connect(&exports_, &ExportQueue::jobChanged, this, [this](const QString &id) {
        updateJobs();
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
    if (settings_.autoRecord)
        QTimer::singleShot(0, this, &MainWindow::toggleRecording);
}
MainWindow::~MainWindow() {
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
            exports_.submitCompression(selectedPath_, encoder);
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
    v->addWidget(jobs_);
    button("Cancel selected job", v, [this] {
        auto *item = jobs_->currentItem();
        if (item)
            exports_.cancel(item->data(Qt::UserRole).toString());
    });
    button("Retry Copy", v, [this] {
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
    for (int i = 0; i < registry_.displays.size(); ++i) {
        auto &d = registry_.displays[i];
        display_->addItem(QString("%1 · %2 × %3 · %4 Hz%5")
                              .arg(d.name)
                              .arg(d.width)
                              .arg(d.height)
                              .arg(d.refresh)
                              .arg(d.hdr ? " · HDR" : ""),
                          i);
    }
    selectValue(display_, settings_.monitor);
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
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QKeySequenceEdit(QKeySequence(initial));
        edit->setMaximumSequenceLength(1);
        edit->setFont(mono());
        l->addWidget(edit);
        button("Apply", l, [this, edit, command] {
            QString text = edit->keySequence().toString(QKeySequence::PortableText);
            QString error;
            if (command == "ptt") {
                UINT mods = 0, key = 0;
                if (!Hotkeys::parse(text, mods, key)) {
                    showError("Choose a supported push-to-talk binding");
                    return;
                }
                if (recorder_.active()) {
                    showError("Stop recording before reassigning push-to-talk so its active binding remains "
                              "explicit.");
                    return;
                }
                settings_.pttHotkey = text;
                for (auto &a : settings_.audio) {
                    a.pushKey = int(key);
                    a.pushModifiers = int(mods);
                }
            } else {
                if (!hotkeys_.assign(command, text, &error)) {
                    showError(error);
                    return;
                }
                if (command == "toolbar")
                    settings_.overlayHotkey = text;
                else
                    settings_.recordHotkey = text;
            }
            if (!settings_.save())
                showError("Could not persist shortcut settings");
            else
                status_->setText("Shortcut applied");
        });
        shortcutForm->addRow(label, row);
    };
    binding("Clipping toolbar", "toolbar", settings_.overlayHotkey);
    binding("Start / stop recording", "record", settings_.recordHotkey);
    binding("Microphone push-to-talk", "ptt", settings_.pttHotkey);
    v->addWidget(shortcuts);
    auto *startup = new QCheckBox("Launch hidden at sign-in");
    startup->setChecked(settings_.startup);
    auto *record = new QCheckBox("Start recording automatically when launched");
    record->setChecked(settings_.autoRecord);
    auto *notifications = new QCheckBox("Show Windows notifications");
    notifications->setChecked(settings_.notifications);
    v->addWidget(startup);
    v->addWidget(record);
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
    windows_->clear();
    for (auto pair : captureWindows())
        windows_->addItem(pair.second, QVariant::fromValue(qulonglong(pair.first)));
    selectValue(windows_, previous);
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
Settings MainWindow::readRecording() const {
    Settings s = settings_;
    s.source = source_->currentData().toString();
    s.monitor = display_->currentData().toInt();
    s.window = quintptr(windows_->currentData().toULongLong());
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
    if (recorder_.active()) {
        recorder_.stop();
        updateStatus();
        return;
    }
    try {
        auto s = readRecording();
        const bool approved = std::exchange(resetApproved_, false);
        if (!approveSdrRecording(s))
            return;
        if (s.encoder == "auto") {
            s.encoder = registry_.automatic(s.hdr);
            if (s.encoder.isEmpty())
                throw std::runtime_error("Run Test Encoders and Calibrate Capture before automatic "
                                         "recording, or choose a tested encoder explicitly.");
            auto *candidate = registry_.find(s.encoder);
            bool matched = false;
            if (candidate)
                for (auto value : candidate->tested) {
                    const auto profile = value.toObject();
                    if (profile.contains("capture") && profile["adapter"].toInt() == s.monitor &&
                        profile["fps"].toInt() == s.fps && profile["requestedHeight"].toInt() == s.height &&
                        profile["source"].toString() == s.source &&
                        profile["backend"].toString() == s.backend &&
                        profile["quality"].toString() == s.quality && profile["audio"] == s.json()["audio"] &&
                        profile["hdr"].toBool() == s.hdr)
                        matched = true;
                }
            if (!matched)
                throw std::runtime_error("The capture profile changed since calibration. Calibrate the "
                                         "selected settings before using automatic recording.");
        }
        auto *c = registry_.find(s.encoder);
        if (!c || !c->encoded)
            throw std::runtime_error("Selected encoder has not passed its test");
        if (!c->containers.contains(s.container))
            throw std::runtime_error("Selected codec is incompatible with the chosen export container");
        if (s.monitor < 0 || s.monitor >= registry_.displays.size())
            throw std::runtime_error("Select an attached capture display");
        auto display = registry_.displays[s.monitor];
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
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
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
    auto registry = registry_;
    cancelDiscovery_ = false;
    discovery_ = QThread::create([this, selected, choices, registry]() mutable {
        double best = 1e30;
        QString winner;
        QJsonArray results;
        for (auto name : choices) {
            if (cancelDiscovery_)
                break;
            Settings s = selected;
            s.encoder = name;
            QTemporaryDir tmp;
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
    recordStats_->setStyleSheet(s.error.isEmpty() ? (s.active ? "color:#87d7aa" : "color:#c7ccd5")
                                                  : "color:#ff9b9b");
    tray_->setToolTip(s.active ? "Recording · " + durationText(s.elapsed) : "Recording off");
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
    raise();
    activateWindow();
    tabs_->setCurrentIndex(0);
    selectedPath_ = path;
    refreshLibrary();
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
                        exports_.submitEdit(original, request, chosen, "High");
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
    else {
        hide();
        event->ignore();
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
ClipToolbar::ClipToolbar(MainWindow *owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), owner_(owner) {
    setWindowTitle("Clipping toolbar");
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    record_ = button("Start Recording", v, [this] {
        owner_->toggleRecording();
        hide();
    });
    auto selector = [&](const QString &label, QComboBox *&box) {
        v->addWidget(new QLabel(label));
        box = new QComboBox;
        box->setFont(mono());
        box->setMinimumHeight(34);
        box->setAccessibleName(label);
        v->addWidget(box);
    };
    selector("Container", container_);
    selector("Frame rate", fps_);
    selector("Resolution", resolution_);
    selector("Duration", duration_);
    available_ = new QLabel;
    available_->setWordWrap(true);
    available_->setFont(mono());
    v->addWidget(available_);
    compress_ = button("Auto Compress", v, [this] { submit(true); });
    compress_->setToolTip(
        "Save and validate the original, then attempt a same-codec copy within 20 MB and the quality guard");
    save_ = button("Save Clip", v, [this] { submit(); });
    button("Show Folder", v,
           [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(owner_->settings_.library)); });
    auto *advanced = new QWidget(this, Qt::Popup);
    auto *a = new QFormLayout(advanced);
    quality_ = new QComboBox;
    quality_->addItems({"Maximum", "High", "Balanced", "Compact"});
    encoder_ = new QComboBox;
    encoder_->setFont(mono());
    a->addRow("Export quality", quality_);
    a->addRow("Codec / implementation", encoder_);
    auto *explanation = new QLabel("Export settings apply only to this clip. Higher resolution, extra frames "
                                   "and lost detail cannot be recovered.");
    explanation->setWordWrap(true);
    explanation->setMaximumWidth(360);
    a->addRow(explanation);
    button("Quality and Codec…", v, [this, advanced] {
        advanced->move(mapToGlobal(QPoint(width() + 8, 20)));
        if (!excludeFromCapture(quintptr(advanced->winId()))) {
            owner_->showError("Windows could not exclude the options panel from capture");
            return;
        }
        advanced->show();
    });
    button("Dismiss", v, [this] { hide(); });
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
        available_->setText(QString("Available: %1\nThis clip: %2\nEndpoint: %3")
                                .arg(durationText(pin_.duration()))
                                .arg(durationText(qMin(double(seconds), pin_.duration())))
                                .arg(endpoint_, 0, 'f', 3));
    });
    connect(encoder_, &QComboBox::currentIndexChanged, this, [this] {
        auto *cap = owner_->registry_.find(encoder_->currentData().toString());
        QString selected = container_->currentData().toString();
        container_->clear();
        if (cap)
            for (auto name : {"mp4", "mkv", "webm", "mov"})
                addChoice(container_, QString(name).toUpper(), name, cap->containers.contains(name),
                          "Selected codec is incompatible with this container");
        selectValue(container_, selected);
        if (container_->currentIndex() >= 0) {
            auto *model = qobject_cast<QStandardItemModel *>(container_->model());
            if (model && !model->item(container_->currentIndex())->isEnabled())
                for (int i = 0; i < container_->count(); ++i)
                    if (model->item(i)->isEnabled()) {
                        container_->setCurrentIndex(i);
                        break;
                    }
        }
    });
    setFixedWidth(230);
}
ClipToolbar::~ClipToolbar() {
    release();
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
    release();
    QWidget::hideEvent(event);
}
void ClipToolbar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
    } else
        QWidget::keyPressEvent(event);
}
void ClipToolbar::open() {
    application_ = foregroundApplication();
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
    record_->setText(stats.active ? "●  Stop Recording" : "Start Recording");
    record_->setStyleSheet(stats.active ? "color:#ff9b9b" : "color:#87d7aa");
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
    selectValue(container_, recorded_.container);
    available_->setText(QString("Available: %1\nThis clip: %2")
                            .arg(durationText(pin_.duration()))
                            .arg(durationText(qMin(30., pin_.duration()))));
    save_->setEnabled(!pin_.id.isEmpty());
    compress_->setEnabled(!pin_.id.isEmpty());
    save_->setToolTip(pin_.id.isEmpty() ? "Start recording and buffer footage before saving"
                                        : "Save the selected footage ending when this toolbar opened");
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (application_.window) {
        auto *handle = QGuiApplication::screenAt(
            QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 1, 1).center());
        Q_UNUSED(handle);
        HMONITOR active = MonitorFromWindow(HWND(application_.window), MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(active, &info))
            screen = QGuiApplication::screenAt(QPoint((info.rcMonitor.left + info.rcMonitor.right) / 2,
                                                      (info.rcMonitor.top + info.rcMonitor.bottom) / 2));
    }
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    auto area = screen->availableGeometry();
    adjustSize();
    move(area.left() + 16, area.top() + qMax(12, (area.height() - height()) / 2));
    if (!excludeFromCapture(quintptr(winId()))) {
        owner_->showError("Windows could not exclude the toolbar from monitor capture");
        release();
        return;
    }
    show();
    raise();
    activateWindow();
    save_->setFocus();
}
void ClipToolbar::submit(bool compress) {
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
        auto source = pin_;
        source.requested = duration_->currentData().toInt();
        source.start = qMax(source.start, source.end - source.requested);
        owner_->exports_.submitSave(source, output, application_.name, application_.executable, compress);
        pin_ = {};
        hide();
        owner_->status_->setText("Clip queued with fixed endpoint · " + durationText(source.duration()));
    } catch (const std::exception &e) {
        owner_->showError(QString::fromUtf8(e.what()));
    }
}
} // namespace clip
