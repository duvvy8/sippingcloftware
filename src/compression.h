#pragma once
#include "edit.h"
namespace clip {
struct CompressionCandidate {
    int width = 0, height = 0;
    double fps = 0;
    qint64 bitrate = 0;
    QJsonObject json() const;
};
struct QualityScore {
    double mean = 0, fifthPercentile = 0;
    int frames = 0;
    bool passed() const {
        return frames > 0 && mean >= 90 && fifthPercentile >= 80;
    }
    QJsonObject json() const;
};
struct CompressionResult {
    QString state, reason, output;
    MediaInfo media;
    QJsonArray attempts;
    QualityScore quality;
};
QList<CompressionCandidate> compressionCandidates(const MediaInfo &, qint64 videoBudget);
QualityScore readQualityScore(const QJsonObject &);
QualityScore scoreMedia(const QString &reference, const QString &candidate, const MediaInfo &,
                        double start = 0, double duration = 0, std::atomic_bool *cancel = nullptr);
CompressionResult compressMedia(const QString &original, const QString &output, const QString &encoder,
                                std::atomic_bool *cancel = nullptr,
                                std::function<void(QString, QString, double)> progress = {});
} // namespace clip
