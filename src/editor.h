#pragma once
#include "edit.h"
#include <QtWidgets>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
namespace clip {
class CropPreview : public QWidget {
    Q_OBJECT
    QImage image_;
    QSize source_;
    QRect region_;
    bool enabled_ = false, dragging_ = false;
    QPoint anchor_;
    QRect imageRect() const;
    QPoint sourcePoint(QPoint) const;

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  public:
    explicit CropPreview(QWidget *parent = nullptr);
    void setImage(QImage, QSize);
    void setRegion(QRect);
    void setCropEnabled(bool);
  signals:
    void regionChanged(QRect);
};
class EditorDialog : public QDialog {
    Q_OBJECT
    friend class UiTests;
    QString path_, encoder_;
    MediaInfo info_;
    QDoubleSpinBox *start_, *end_;
    QComboBox *mode_, *aspect_;
    QCheckBox *crop_, *fit_, *mix_;
    QLabel *result_;
    CropPreview *preview_;
    QSpinBox *x_, *y_, *w_, *h_;
    QTableWidget *audio_;
    QMediaPlayer *player_ = nullptr;
    QVideoWidget *video_;
    QStackedWidget *visual_;
    void updateResult();
    EditRequest request() const;

  public:
    EditorDialog(const QString &, const QString &encoder, int initialTab, QWidget *parent = nullptr);
    ~EditorDialog();
  signals:
    void exportRequested(clip::EditRequest request);
};
} // namespace clip
Q_DECLARE_METATYPE(clip::EditRequest)
