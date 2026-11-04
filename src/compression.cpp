#include "compression.h"
#include "hdr.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
namespace clip {
QJsonObject CompressionCandidate::json() const {
    return {{"width", width}, {"height", height}, {"fps", fps}, {"bitrate", bitrate}};
}
QJsonObject QualityScore::json() const {
    return {{"mean", mean}, {"fifthPercentile", fifthPercentile}, {"frames", frames}, {"passed", passed()}};
}
QList<CompressionCandidate> compressionCandidates(const MediaInfo &m, qint64 budget) {
    if (m.duration <= 0 || m.fps <= 0 || m.width < 2 || m.height < 2 || budget <= 0)
        return {};
    QList<QSize> sizes{{m.width, m.height}};
    int shortSide = qMin(m.width, m.height);
    for (int edge : {2160, 1440, 1080, 900, 720, 540, 480}) {
        if (edge >= shortSide)
            continue;
        double factor = double(edge) / shortSide;
        QSize size(qMax(2, qRound(m.width * factor / 2) * 2), qMax(2, qRound(m.height * factor / 2) * 2));
        if (!sizes.contains(size))
            sizes << size;
    }
    QList<CompressionCandidate> candidates;
    const qint64 rate = qMax(qint64(1), qint64(double(budget) * 8 / m.duration));
    auto add = [&](QSize size, double fps, bool strengths) {
        for (double strength : strengths ? QList<double>{1.15, 1., .85} : QList<double>{1., .85})
            candidates << CompressionCandidate{size.width(), size.height(), fps, qint64(rate * strength)};
    };
    add(sizes.first(), m.fps, true);
    double stageRate = qMin(60., m.fps);
    if (m.fps > 60)
        add(sizes.first(), stageRate, true);
    for (int i = 1; i < sizes.size(); ++i)
        add(sizes[i], stageRate, false);
    for (double fps : {30., 24., 20., 15.})
        if (fps < stageRate - .001)
            for (auto size : sizes)
                add(size, fps, false);
    return candidates;
}
QualityScore readQualityScore(const QJsonObject &json) {
    QList<double> values;
    for (auto frame : json["frames"].toArray()) {
        auto metric = frame.toObject()["metrics"].toObject()["vmaf"];
        if (!metric.isDouble() || !std::isfinite(metric.toDouble()))
            throw std::runtime_error("Quality analysis returned an invalid frame score");
        values << metric.toDouble();
    }
    if (values.isEmpty())
        throw std::runtime_error("Quality analysis returned no frame scores");
    QualityScore score;
    score.frames = int(values.size());
    for (double value : values)
        score.mean += value;
    score.mean /= score.frames;
    std::sort(values.begin(), values.end());
    score.fifthPercentile = values[qMax(0, int(std::ceil(.05 * score.frames)) - 1)];
    return score;
}
namespace {
void requireProcess(const ProcessResult &p, const char *context) {
    if (p.code != 0)
        throw std::runtime_error(
            p.cancelled ? "Cancelled"
                        : (QString::fromUtf8(context) + ": " + QString::fromUtf8(p.output)).toStdString());
}
QString number(double v) {
    return QString::number(v, 'f', 9);
}
QString formatFor(const QString &file) {
    QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix == "mkv")
        return "matroska";
    if (QStringList{"mp4", "mov", "webm"}.contains(suffix))
        return suffix;
    throw std::runtime_error("Compression supports original MP4, MKV, WebM and MOV containers");
}
QString displayFilter(const MediaInfo &m) {
    QString transform;
    if (m.hdr) {
        transform = sdrToneMapFilter();
        transform.replace("tonemap=tonemap=hable:desat=0", "tonemap=tonemap=hable:desat=0:peak=10");
        transform += ',';
    }
    return transform + QString("fps=%1:round=near,scale=%2:%3:flags=lanczos,setsar=1,format=yuv420p,settb="
                               "AVTB,setpts=PTS-STARTPTS")
                           .arg(number(m.fps))
                           .arg(m.width)
                           .arg(m.height);
}
QList<double> samples(const QString &file, const MediaInfo &m, std::atomic_bool *cancel) {
    if (m.duration <= 6)
        return {0};
    QTemporaryDir tmp;
    if (!tmp.isValid())
        throw std::runtime_error("Cannot create motion analysis directory");
    auto p = runProcess(
        ffmpegPath(),
        {"-nostdin", "-v", "error", "-threads", "2", "-i", file, "-an", "-filter_threads", "2", "-vf",
         "fps=15,scale=320:-2,signalstats,metadata=mode=print:key=lavfi.signalstats.YDIF:file=motion.txt",
         "-f", "null", "-"},
        0, cancel, {}, tmp.path());
    requireProcess(p, "Motion analysis failed");
    QFile log(tmp.filePath("motion.txt"));
    if (!log.open(QIODevice::ReadOnly))
        throw std::runtime_error("Motion analysis report is unavailable");
    QMap<int, QPair<double, int>> bins;
    double time = 0;
    while (!log.atEnd()) {
        auto line = QString::fromUtf8(log.readLine());
        if (line.contains("pts_time:"))
            time = line.section("pts_time:", 1).trimmed().toDouble();
        if (line.startsWith("lavfi.signalstats.YDIF=")) {
            auto &bin = bins[int(time / 2)];
            bin.first += line.section('=', 1).toDouble();
            ++bin.second;
        }
    }
    double best = -1, highMotion = 0;
    for (auto it = bins.begin(); it != bins.end(); ++it) {
        double average = it.value().first / qMax(1, it.value().second);
        if (average > best) {
            best = average;
            highMotion = qBound(0., it.key() * 2., m.duration - 2);
        }
    }
    if (best < 0)
        throw std::runtime_error("Motion analysis produced no representative intervals");
    QList<double> starts{0, qMax(0., m.duration / 2 - 1), qMax(0., m.duration - 2), highMotion};
    std::sort(starts.begin(), starts.end());
    for (int i = int(starts.size()) - 1; i > 0; --i)
        if (starts[i] - starts[i - 1] < .25)
            starts.removeAt(i);
    return starts;
}
void encode(const QString &original, const QString &output, const QString &encoder, const MediaInfo &m,
            const CompressionCandidate &c, const QString &format, double start, double duration, bool sample,
            std::atomic_bool *cancel, std::function<void(double)> progress) {
    QStringList args{"-v", "error", "-y", "-threads", "2"};
    if (sample)
        args << "-ss" << number(start);
    args << "-i" << original;
    if (sample)
        args << "-t" << number(duration);
    args << "-map" << "0:v:0";
    if (!sample)
        args << "-map" << "0:a?";
    args << "-map_metadata" << "0" << "-c:a" << "copy";
    args << encoderArguments(encoder, "High", c.bitrate, true);
    args << "-threads:v" << "4" << "-filter_threads" << "2";
    if (encoder == "libx265")
        args << "-x265-params" << "pools=2:frame-threads=2";
    QString filter =
        QString("scale=%1:%2:flags=lanczos,fps=%3:round=near").arg(c.width).arg(c.height).arg(number(c.fps));
    args << "-vf" << filter << colorArguments(m) << "-f" << format << output;
    requireProcess(mediaProcess(args, duration, cancel, progress), "Candidate encoding failed");
}
QString incompatible(const MediaInfo &m, const MediaInfo &out, const CompressionCandidate &c) {
    if (out.bytes > SizeLimit)
        return "Actual finalized file exceeds 20,000,000 bytes";
    if (out.codecId != m.codecId || out.container != m.container)
        return "Codec or container did not match the original";
    if (out.audioTracks != m.audioTracks || out.audioStreams != m.audioStreams)
        return "Intended audio tracks or channel layouts changed";
    if (out.audioPayloadHashes != m.audioPayloadHashes)
        return "Retained audio packet payloads changed";
    if (std::abs(out.displayAspect - m.displayAspect) > .002)
        return "Display aspect ratio changed";
    if (out.primaries != m.primaries || out.transfer != m.transfer || out.matrix != m.matrix ||
        out.range != m.range || out.hdr != m.hdr || out.hdrMetadata != m.hdrMetadata)
        return "Colour characteristics or available HDR metadata changed";
    if (out.width != c.width || out.height != c.height || std::abs(out.fps - c.fps) > .02)
        return "Encoded dimensions or frame rate did not match the candidate";
    if (std::abs(out.duration - m.duration) > 1. / c.fps + qMax(m.audioRounding, out.audioRounding) + .001)
        return "Candidate duration is outside one output frame plus audio-packet rounding";
    return {};
}
} // namespace
QualityScore scoreMedia(const QString &reference, const QString &candidate, const MediaInfo &m, double start,
                        double duration, std::atomic_bool *cancel) {
    QTemporaryDir tmp;
    if (!tmp.isValid())
        throw std::runtime_error("Cannot create quality analysis directory");
    QStringList args{"-nostdin", "-v", "error", "-threads", "2", "-i", candidate, "-threads", "2"};
    if (start > 0)
        args << "-ss" << number(start);
    args << "-i" << reference;
    QString transform = displayFilter(m);
    QString graph = "[0:v]" + transform + "[dist];[1:v]";
    if (duration > 0)
        graph += "trim=duration=" + number(duration) + ',';
    graph += transform + "[ref];[dist][ref]libvmaf=log_fmt=json:log_path=quality.json:n_threads=2:shortest=1";
    args << "-an" << "-filter_complex_threads" << "2" << "-lavfi" << graph << "-f" << "null" << "-";
    requireProcess(runProcess(ffmpegPath(), args, 0, cancel, {}, tmp.path()), "Quality analysis failed");
    auto score = readQualityScore(readJson(tmp.filePath("quality.json")));
    double expectedDuration = duration > 0 ? duration : m.duration;
    int allowance = int(std::ceil(m.audioRounding * m.fps)) + 2;
    if (score.frames + allowance < int(std::floor(expectedDuration * m.fps)))
        throw std::runtime_error("Quality analysis did not cover the complete expected interval");
    return score;
}
CompressionResult compressMedia(const QString &original, const QString &output, const QString &encoder,
                                std::atomic_bool *cancel,
                                std::function<void(QString, QString, double)> progress) {
    auto report = [&](QString state, QString text, double value = 0) {
        if (cancel && cancel->load())
            throw std::runtime_error("Cancelled");
        if (progress)
            progress(state, text, value);
    };
    report("Analysing", "Validating the saved original and retained audio budget");
    const auto identity = fileIdentity(original);
    if (identity.isEmpty() || QFileInfo(original).absoluteFilePath() == QFileInfo(output).absoluteFilePath())
        throw std::runtime_error("Compression requires a saved original and a distinct output path");
    auto m = inspect(original, true);
    if (m.duration <= 0 || m.fps <= 0 || !std::isfinite(m.duration) || !std::isfinite(m.fps))
        throw std::runtime_error("Original has no usable video duration or frame rate");
    if (!decodeValidate(original, cancel))
        throw std::runtime_error(cancel && cancel->load() ? "Cancelled"
                                                          : "Original failed complete decoding validation");
    CompressionResult result;
    result.media = m;
    if (m.bytes <= SizeLimit) {
        if (fileIdentity(original) != identity)
            throw std::runtime_error("Original changed during validation");
        result.state = "Already within limit";
        result.output = original;
        result.reason = "Original already meets the 20,000,000-byte limit";
        return result;
    }
    qint64 allowance = qMax(qint64(65536), TargetSize / 100);
    qint64 budget = TargetSize - m.audioBytes - allowance;
    result.state = "Cannot meet constraints";
    if (budget <= 0) {
        result.reason = "Retained audio and container allowance consume the supported 19.5 MB target budget";
        return result;
    }
    const AVCodec *codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (!codec || codec->id != m.codecId)
        throw std::runtime_error("Compression requires an available implementation of the original codec");
    QString format = formatFor(original);
    if (formatFor(output) != format)
        throw std::runtime_error("Compression output must preserve the original container");
    if (QFileInfo::exists(output))
        throw std::runtime_error("Compression output already exists");
    auto starts = samples(original, m, cancel);
    auto candidates = compressionCandidates(m, budget);
    // FFV1 is lossless: bitrate changes do not alter its output. Test each geometry/rate once.
    if (m.codecId == AV_CODEC_ID_FFV1) {
        QSet<QString> seen;
        for (int i = 0; i < candidates.size();) {
            const auto &c = candidates[i];
            QString key = QString("%1/%2/%3").arg(c.width).arg(c.height).arg(c.fps, 0, 'f', 9);
            if (seen.contains(key))
                candidates.removeAt(i);
            else {
                seen.insert(key);
                ++i;
            }
        }
    }
    QTemporaryDir tmp(QFileInfo(output).absolutePath() + "/.compress-XXXXXX");
    if (!tmp.isValid())
        throw std::runtime_error("Cannot create candidate storage beside the original");
    for (const auto &c : candidates) {
        QString description = QString("%1 · %2×%3 · %4 fps · %5 bit/s")
                                  .arg(encoder)
                                  .arg(c.width)
                                  .arg(c.height)
                                  .arg(c.fps, 0, 'f', 3)
                                  .arg(c.bitrate);
        QJsonObject attempt = c.json();
        bool shortlist = true;
        QJsonArray scores;
        for (double start : starts) {
            report("Encoding", "Representative sample: " + description);
            QString sample = tmp.filePath("sample." + QFileInfo(original).suffix());
            double duration = qMin(m.duration <= 6 ? m.duration : 2., m.duration - start);
            encode(original, sample, encoder, m, c, format, start, duration, true, cancel, {});
            report("Validating", "Scoring representative sample: " + description);
            auto score = scoreMedia(original, sample, m, start, duration, cancel);
            scores << score.json();
            QFile::remove(sample);
            if (!score.passed()) {
                shortlist = false;
                result.reason = "Representative samples failed the VMAF mean 90 / fifth-percentile 80 guard";
                break;
            }
        }
        attempt["samples"] = scores;
        if (shortlist) {
            QString candidate = tmp.filePath("candidate." + QFileInfo(original).suffix());
            report("Encoding", "Complete candidate: " + description);
            encode(original, candidate, encoder, m, c, format, 0, m.duration, false, cancel,
                   [&](double p) { report("Encoding", description, p); });
            report("Validating", "Checking complete file, duration, tracks, colour and byte limit");
            auto out = inspect(candidate, true);
            auto rejection = incompatible(m, out, c);
            if (rejection.isEmpty() && !decodeValidate(candidate, cancel))
                throw std::runtime_error(
                    cancel && cancel->load() ? "Cancelled" : "Candidate failed complete decoding validation");
            if (rejection.isEmpty()) {
                report("Validating", "Scoring every frame of the complete candidate");
                auto score = scoreMedia(original, candidate, m, 0, 0, cancel);
                attempt["quality"] = score.json();
                if (score.passed()) {
                    if (fileIdentity(original) != identity)
                        throw std::runtime_error("Original changed while compression was processing");
                    report("Validating", "Finalizing verified compressed copy", 1);
                    if (!QFile::rename(candidate, output))
                        throw std::runtime_error("Cannot finalize the verified compressed copy");
                    result.state = "Compressed";
                    result.output = output;
                    result.media = out;
                    result.quality = score;
                    attempt["accepted"] = true;
                    result.attempts << attempt;
                    return result;
                }
                rejection = "Complete output failed the VMAF mean 90 / fifth-percentile 80 guard";
            }
            result.reason = rejection;
            attempt["rejection"] = rejection;
            QFile::remove(candidate);
        }
        result.attempts << attempt;
    }
    if (fileIdentity(original) != identity)
        throw std::runtime_error("Original changed during compression analysis");
    result.reason = "Supported same-codec search did not meet all constraints. " + result.reason;
    return result;
}
} // namespace clip
