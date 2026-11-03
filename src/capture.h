#pragma once
#include "registry.h"
#include <d3d11.h>
#include <wrl/client.h>
namespace clip {
struct CapturedFrame {
    QByteArray pixels;
    int width = 0, height = 0, stride = 0;
    bool floating = false;
    double timestamp = 0;
};
class Capture {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    Capture();
    ~Capture();
    void open(const Settings &, const Registry &);
    bool next(CapturedFrame &);
    void close();
    QString backend() const;
};
} // namespace clip
