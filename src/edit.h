#pragma once
#include "media.h"
namespace clip {
struct EditRequest {
    double start = 0, end = 0;
    bool fast = false, crop = false, fit = true, mixAudio = false;
    QRect region;
    QString aspect = "Source";
    QList<int> tracks;
    QList<double> gains;
    QJsonObject json() const;
    static EditRequest fromJson(const QJsonObject &);
};
struct TrimBounds {
    double start = 0, end = 0;
    bool rounded = false;
};
TrimBounds trimBounds(const QString &, double start, double end, bool fast);
QSize aspectSize(QSize source, const QString &aspect);
MediaInfo editMedia(const QString &original, const QString &output, const EditRequest &,
                    const QString &encoder, const QString &quality, std::atomic_bool *cancel = nullptr,
                    std::function<void(double)> progress = {});
QStringList encoderArguments(const QString &encoder, const QString &quality, qint64 bitrate = 0,
                             bool offline = true);
ProcessResult mediaProcess(QStringList args, double duration, std::atomic_bool *cancel = nullptr,
                           std::function<void(double)> progress = {});
} // namespace clip
