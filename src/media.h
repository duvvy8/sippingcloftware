#pragma once
#include "common.h"
#include <QImage>
namespace clip {
struct MediaInfo {
    double duration = 0, fps = 0, audioRounding = 0, displayAspect = 0;
    int width = 0, height = 0, videoStream = -1, codecId = 0, audioTracks = 0;
    qint64 bytes = 0, audioBytes = 0;
    QString codec, container, pixelFormat, transfer, primaries, matrix;
    QJsonObject hdrMetadata;
    QJsonArray audioStreams;
    QStringList audioPayloadHashes;
    int range = 0;
    bool hdr = false;
    QJsonObject json() const;
};
MediaInfo inspect(const QString &, bool countAudio = false);
QImage thumbnail(const QString &, double seconds = 0);
bool decodeValidate(const QString &, std::atomic_bool *cancel = nullptr);
void configureEncoder(AVCodecContext *, const QString &quality, qint64 bitrate = 0, bool offline = false);
} // namespace clip
