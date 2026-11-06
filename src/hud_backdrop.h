#pragma once
#include <QList>
#include <QRect>
#include <memory>
#include <windows.h>

namespace clip {
class HudBackdrop {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    HudBackdrop();
    ~HudBackdrop();
    bool update(HWND hud, const QList<QRect> &surfaces, qreal scale);
    void hide();
};
} // namespace clip
