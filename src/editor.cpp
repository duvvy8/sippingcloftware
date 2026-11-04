#include "editor.h"
namespace clip {
CropPreview::CropPreview(QWidget *p) : QWidget(p) {
    setMinimumSize(280, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Visible crop selection");
}
QRect CropPreview::imageRect() const {
    auto size = source_.scaled(this->size(), Qt::KeepAspectRatio);
    return {QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}
QPoint CropPreview::sourcePoint(QPoint p) const {
    auto r = imageRect();
    return {qBound(0, (p.x() - r.x()) * source_.width() / qMax(1, r.width()), source_.width()),
            qBound(0, (p.y() - r.y()) * source_.height() / qMax(1, r.height()), source_.height())};
}
void CropPreview::setImage(QImage image, QSize source) {
    image_ = image;
    source_ = source;
    region_ = {QPoint(0, 0), source};
    update();
}
void CropPreview::setRegion(QRect region) {
    region_ = region;
    update();
}
void CropPreview::setCropEnabled(bool enabled) {
    enabled_ = enabled;
    update();
}
void CropPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(14, 16, 19));
    auto r = imageRect();
    p.drawImage(r, image_);
    if (enabled_ && source_.isValid()) {
        QRect selected(r.x() + region_.x() * r.width() / source_.width(),
                       r.y() + region_.y() * r.height() / source_.height(),
                       region_.width() * r.width() / source_.width(),
                       region_.height() * r.height() / source_.height());
        QPainterPath outside;
        outside.addRect(r);
        QPainterPath inside;
        inside.addRect(selected);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 130));
        p.setPen(QPen(QColor(140, 182, 236), 2));
        p.drawRect(selected);
    }
}
void CropPreview::mousePressEvent(QMouseEvent *e) {
    if (enabled_ && e->button() == Qt::LeftButton) {
        dragging_ = true;
        anchor_ = sourcePoint(e->position().toPoint());
    }
}
void CropPreview::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_) {
        QRect r = QRect(anchor_, sourcePoint(e->position().toPoint())).normalized();
        r.setX(r.x() & ~1);
        r.setY(r.y() & ~1);
        r.setWidth(qMax(2, r.width() & ~1));
        r.setHeight(qMax(2, r.height() & ~1));
        region_ = r.intersected(QRect(QPoint(0, 0), source_));
        update();
        emit regionChanged(region_);
    }
}
void CropPreview::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
}
EditorDialog::EditorDialog(const QString &path, const QString &encoder, int tab, QWidget *parent)
    : QDialog(parent), path_(path), encoder_(encoder), info_(inspect(path)) {
    setWindowTitle("Edit clip");
    setAttribute(Qt::WA_DeleteOnClose);
    const auto available = screen()->availableGeometry();
    resize(qMin(960, available.width() - 32), qMin(760, available.height() - 48));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Edits create a new file. The original remains unchanged."));
    visual_ = new QStackedWidget;
    preview_ = new CropPreview;
    preview_->setImage(thumbnail(path, 0.5), {info_.width, info_.height});
    video_ = new QVideoWidget;
    visual_->addWidget(preview_);
    visual_->addWidget(video_);
    layout->addWidget(visual_, 1);
    auto *previewButton = new QPushButton("Preview / Stop");
    layout->addWidget(previewButton);
    auto *tabs = new QTabWidget;
    auto scrollPage = [](QWidget *child) {
        auto *area = new QScrollArea;
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(child);
        return area;
    };
    auto *trimPage = new QWidget;
    auto *trim = new QFormLayout(trimPage);
    start_ = new QDoubleSpinBox;
    end_ = new QDoubleSpinBox;
    for (auto *s : {start_, end_}) {
        s->setDecimals(3);
        s->setRange(0, info_.duration);
        s->setSuffix(" s");
        s->setFont(QFont("Geist Mono", 10));
    }
    end_->setValue(info_.duration);
    mode_ = new QComboBox;
    mode_->addItem("Exact trim · re-encode selected boundaries", false);
    mode_->addItem("Fast trim · packet copy, round to surrounding keyframes", true);
    trim->addRow("Start", start_);
    trim->addRow("End", end_);
    trim->addRow("Method", mode_);
    sdr_ = new QCheckBox("Create an explicit tone-mapped SDR sharing copy (H.264 / MP4)");
    sdr_->setEnabled(info_.hdr);
    sdr_->setToolTip(info_.hdr ? "Converts HDR pixels into BT.709 SDR; keeps the original HDR file"
                               : "This clip is already SDR");
    trim->addRow(sdr_);
    tabs->addTab(scrollPage(trimPage), "Trim");
    auto *cropPage = new QWidget;
    auto *cropLayout = new QFormLayout(cropPage);
    aspect_ = new QComboBox;
    aspect_->addItems({"Source", "16:9", "9:16", "1:1"});
    aspect_->setFont(QFont("Geist Mono", 10));
    fit_ = new QCheckBox("Fit the entire image into the output aspect ratio");
    fit_->setChecked(true);
    crop_ = new QCheckBox("Crop the visible selected region (drag on the image or use coordinates)");
    cropLayout->addRow("Output aspect ratio", aspect_);
    cropLayout->addRow(fit_);
    cropLayout->addRow(crop_);
    x_ = new QSpinBox;
    y_ = new QSpinBox;
    w_ = new QSpinBox;
    h_ = new QSpinBox;
    x_->setRange(0, info_.width - 2);
    y_->setRange(0, info_.height - 2);
    w_->setRange(2, info_.width);
    h_->setRange(2, info_.height);
    w_->setValue(info_.width);
    h_->setValue(info_.height);
    for (auto *s : {x_, y_, w_, h_}) {
        s->setFont(QFont("Geist Mono", 10));
        s->setSingleStep(2);
    }
    cropLayout->addRow("Left", x_);
    cropLayout->addRow("Top", y_);
    cropLayout->addRow("Width", w_);
    cropLayout->addRow("Height", h_);
    tabs->addTab(scrollPage(cropPage), "Crop / Aspect Ratio");
    auto *audioPage = new QWidget;
    auto *audioLayout = new QVBoxLayout(audioPage);
    audio_ = new QTableWidget(info_.audioTracks, 4);
    audio_->setHorizontalHeaderLabels({"Include", "Track", "Gain", "Mute"});
    for (int i = 0; i < info_.audioTracks; ++i) {
        auto *include = new QCheckBox;
        include->setChecked(true);
        audio_->setCellWidget(i, 0, include);
        auto *title = new QTableWidgetItem(
            info_.audioStreams.at(i).toObject()["title"].toString(QString("Track %1").arg(i + 1)));
        title->setFlags(title->flags() & ~Qt::ItemIsEditable);
        audio_->setItem(i, 1, title);
        auto *gain = new QDoubleSpinBox;
        gain->setRange(0, 4);
        gain->setSingleStep(.1);
        gain->setValue(1);
        gain->setFont(QFont("Geist Mono", 10));
        audio_->setCellWidget(i, 2, gain);
        audio_->setCellWidget(i, 3, new QCheckBox);
        connect(include, &QCheckBox::toggled, this, &EditorDialog::updateResult);
        connect(gain, &QDoubleSpinBox::valueChanged, this, &EditorDialog::updateResult);
        connect(qobject_cast<QCheckBox *>(audio_->cellWidget(i, 3)), &QCheckBox::toggled, this,
                &EditorDialog::updateResult);
    }
    audio_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    audioLayout->addWidget(audio_);
    mix_ = new QCheckBox("Create one mixed sharing track from the included tracks");
    audioLayout->addWidget(mix_);
    tabs->addTab(scrollPage(audioPage), "Audio");
    tabs->setMinimumHeight(180);
    tabs->setCurrentIndex(qBound(0, tab, 2));
    layout->addWidget(tabs);
    result_ = new QLabel;
    result_->setWordWrap(true);
    result_->setFont(QFont("Geist Mono", 10));
    layout->addWidget(result_);
    auto *actions = new QDialogButtonBox;
    auto *exportButton = actions->addButton("Export New File", QDialogButtonBox::AcceptRole);
    actions->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, this, [this] {
        try {
            auto r = request();
            auto bounds = trimBounds(path_, r.start, r.end, r.fast);
            if (r.fast && (r.crop || r.aspect != "Source" || r.mixAudio))
                throw std::runtime_error("Choose Exact trim to crop, change aspect ratio, or mix audio.");
            if (r.crop && !QRect(0, 0, info_.width, info_.height).contains(r.region))
                throw std::runtime_error("The selected crop extends beyond the image.");
            if (bounds.rounded &&
                QMessageBox::question(this, "Use rounded boundaries?",
                                      QString("Fast trim will save %1 to %2 seconds (%3 seconds). Continue?")
                                          .arg(bounds.start, 0, 'f', 3)
                                          .arg(bounds.end, 0, 'f', 3)
                                          .arg(bounds.end - bounds.start, 0, 'f', 3),
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes)
                return;
            emit exportRequested(r);
            accept();
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Check edit settings", QString::fromUtf8(e.what()));
        }
    });
    connect(previewButton, &QPushButton::clicked, this, [this] {
        if (player_ && player_->playbackState() == QMediaPlayer::PlayingState) {
            player_->stop();
            visual_->setCurrentWidget(preview_);
            return;
        }
        if (!player_) {
            player_ = new QMediaPlayer(this);
            auto *audio = new QAudioOutput(this);
            player_->setAudioOutput(audio);
            player_->setVideoOutput(video_);
            player_->setSource(QUrl::fromLocalFile(path_));
            connect(player_, &QMediaPlayer::errorOccurred, this,
                    [this](auto, const QString &reason) { result_->setText("Preview failed: " + reason); });
        }
        visual_->setCurrentWidget(video_);
        player_->setPosition(qRound64(start_->value() * 1000));
        player_->play();
    });
    auto updateRegion = [this] {
        preview_->setRegion({x_->value(), y_->value(), w_->value(), h_->value()});
        updateResult();
    };
    for (auto *s : {x_, y_, w_, h_})
        connect(s, &QSpinBox::valueChanged, this, updateRegion);
    connect(preview_, &CropPreview::regionChanged, this, [this](QRect r) {
        QSignalBlocker a(x_), b(y_), c(w_), d(h_);
        x_->setValue(r.x());
        y_->setValue(r.y());
        w_->setValue(r.width());
        h_->setValue(r.height());
        updateResult();
    });
    connect(crop_, &QCheckBox::toggled, this, [this](bool enabled) {
        preview_->setCropEnabled(enabled);
        if (enabled) {
            if (player_)
                player_->stop();
            visual_->setCurrentWidget(preview_);
        }
        updateResult();
    });
    connect(start_, &QDoubleSpinBox::valueChanged, this, &EditorDialog::updateResult);
    connect(end_, &QDoubleSpinBox::valueChanged, this, &EditorDialog::updateResult);
    connect(mode_, &QComboBox::currentIndexChanged, this, &EditorDialog::updateResult);
    connect(aspect_, &QComboBox::currentIndexChanged, this, &EditorDialog::updateResult);
    connect(mix_, &QCheckBox::toggled, this, &EditorDialog::updateResult);
    connect(sdr_, &QCheckBox::toggled, this, &EditorDialog::updateResult);
    updateResult();
}
EditorDialog::~EditorDialog() {
    if (player_) {
        player_->stop();
        player_->setSource({});
    }
}
EditRequest EditorDialog::request() const {
    EditRequest r;
    r.start = start_->value();
    r.end = end_->value();
    r.fast = mode_->currentData().toBool();
    r.crop = crop_->isChecked();
    r.fit = fit_->isChecked();
    r.aspect = aspect_->currentText();
    r.region = {x_->value(), y_->value(), w_->value(), h_->value()};
    r.mixAudio = mix_->isChecked();
    r.sdr = sdr_->isChecked();
    for (int i = 0; i < info_.audioTracks; ++i)
        if (qobject_cast<QCheckBox *>(audio_->cellWidget(i, 0))->isChecked()) {
            r.tracks << i;
            r.gains << (qobject_cast<QCheckBox *>(audio_->cellWidget(i, 3))->isChecked()
                            ? 0.
                            : qobject_cast<QDoubleSpinBox *>(audio_->cellWidget(i, 2))->value());
        }
    return r;
}
void EditorDialog::updateResult() {
    try {
        auto r = request();
        auto bounds = trimBounds(path_, r.start, r.end, r.fast);
        auto size = aspectSize({info_.width, info_.height}, r.aspect);
        if (r.crop && r.aspect == "Source")
            size = r.region.size();
        result_->setText(QString("Output: %1 s · %2 × %3 · %4 audio track(s)%5")
                             .arg(bounds.end - bounds.start, 0, 'f', 3)
                             .arg(size.width())
                             .arg(size.height())
                             .arg(r.mixAudio && !r.tracks.isEmpty() ? 1 : r.tracks.size())
                             .arg(bounds.rounded ? QString("\nKeyframe rounding: %1 → %2 s")
                                                       .arg(bounds.start, 0, 'f', 3)
                                                       .arg(bounds.end, 0, 'f', 3)
                                                 : QString()));
    } catch (const std::exception &e) {
        result_->setText(QString::fromUtf8(e.what()));
    }
}
} // namespace clip
