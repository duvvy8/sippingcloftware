#include "edit.h"
#include <cmath>
namespace clip {
QJsonObject EditRequest::json() const {
    QJsonArray t, g;
    for (int i : tracks)
        t << i;
    for (auto v : gains)
        g << v;
    return {{"start", start},
            {"end", end},
            {"fast", fast},
            {"crop", crop},
            {"fit", fit},
            {"mixAudio", mixAudio},
            {"region", QJsonArray{region.x(), region.y(), region.width(), region.height()}},
            {"aspect", aspect},
            {"tracks", t},
            {"gains", g}};
}
EditRequest EditRequest::fromJson(const QJsonObject &o) {
    EditRequest r;
    r.start = o["start"].toDouble();
    r.end = o["end"].toDouble();
    r.fast = o["fast"].toBool();
    r.crop = o["crop"].toBool();
    r.fit = o["fit"].toBool(true);
    r.mixAudio = o["mixAudio"].toBool();
    r.aspect = o["aspect"].toString("Source");
    auto a = o["region"].toArray();
    if (a.size() == 4)
        r.region = QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
    for (auto v : o["tracks"].toArray())
        r.tracks << v.toInt();
    for (auto v : o["gains"].toArray())
        r.gains << v.toDouble(1);
    return r;
}
QStringList encoderArguments(const QString &name, const QString &quality, qint64 bitrate, bool offline) {
    const auto *codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
    if (!codec)
        throw std::runtime_error("Requested encoder is absent");
    auto *c = avcodec_alloc_context3(codec);
    if (!c)
        throw std::bad_alloc();
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> owner(
        c, [](auto *p) { avcodec_free_context(&p); });
    const QStringList keys{"preset",   "tune",     "rc",      "cq",    "crf",   "qp_i",   "qp_p",
                           "cpu-used", "deadline", "profile", "level", "coder", "context"};
    QMap<QString, QString> defaults;
    auto option = [&](const QString &key) {
        uint8_t *value = nullptr;
        QString text;
        if (c->priv_data && av_opt_get(c->priv_data, key.toUtf8().constData(), 0, &value) >= 0 && value) {
            text = QString::fromUtf8(reinterpret_cast<char *>(value));
            av_free(value);
        }
        return text;
    };
    for (const auto &key : keys)
        defaults[key] = option(key);
    configureEncoder(c, quality, bitrate, offline);
    QStringList args{"-c:v", name};
    if (bitrate > 0)
        args << "-b:v" << QString::number(bitrate);
    for (const auto &key : keys) {
        auto text = option(key);
        if (!text.isEmpty() && text != defaults.value(key))
            args << "-" + key << text;
    }
    if (c->level >= 0)
        args << "-level:v" << QString::number(c->level);
    if (c->flags & AV_CODEC_FLAG_QSCALE)
        args << "-q:v" << QString::number(double(c->global_quality) / FF_QP2LAMBDA);
    else if (c->global_quality > 0)
        args << "-global_quality" << QString::number(c->global_quality);
    return args;
}
ProcessResult mediaProcess(QStringList args, double duration, std::atomic_bool *cancel,
                           std::function<void(double)> progress) {
    args.prepend("-nostdin");
    args.prepend("-hide_banner");
    args << "-progress" << "pipe:1" << "-nostats";
    QByteArray pending;
    return runProcess(ffmpegPath(), args, 0, cancel, [&](QByteArray bytes) {
        pending += bytes;
        int pos;
        while ((pos = pending.indexOf('\n')) >= 0) {
            auto line = pending.left(pos).trimmed();
            pending.remove(0, pos + 1);
            if (line.startsWith("out_time_us=") && duration > 0 && progress)
                progress(qBound(0., line.mid(12).toDouble() / 1000000. / duration, 0.99));
        }
    });
}
TrimBounds trimBounds(const QString &path, double start, double end, bool fast) {
    auto media = inspect(path);
    if (start < 0 || end <= start || end > media.duration + 1. / qMax(1., media.fps))
        throw std::runtime_error("Trim boundaries are outside the source duration");
    TrimBounds result{start, qMin(end, media.duration), false};
    if (!fast)
        return result;
    AVFormatContext *f = nullptr;
    check(avformat_open_input(&f, path.toUtf8().constData(), nullptr, nullptr), "Open trim source");
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> owned(
        f, [](auto *p) { avformat_close_input(&p); });
    check(avformat_find_stream_info(f, nullptr), "Read trim streams");
    int stream = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    check(stream, "Find video stream");
    auto s = f->streams[stream];
    QList<double> keys{0};
    auto p = av_packet_alloc();
    std::unique_ptr<AVPacket, void (*)(AVPacket *)> packet(p, [](auto *v) { av_packet_free(&v); });
    while (av_read_frame(f, p) >= 0) {
        if (p->stream_index == stream && (p->flags & AV_PKT_FLAG_KEY) && p->pts != AV_NOPTS_VALUE) {
            double t = p->pts * av_q2d(s->time_base);
            if (t > 0)
                keys << t;
        }
        av_packet_unref(p);
    }
    keys << media.duration;
    std::sort(keys.begin(), keys.end());
    result.start = 0;
    result.end = media.duration;
    for (double key : keys) {
        if (key <= start + 1e-6)
            result.start = key;
        if (key >= end - 1e-6) {
            result.end = key;
            break;
        }
    }
    result.rounded = std::abs(result.start - start) > 1e-6 || std::abs(result.end - end) > 1e-6;
    return result;
}
QSize aspectSize(QSize source, const QString &aspect) {
    double ratio = 0;
    if (aspect == "16:9")
        ratio = 16. / 9;
    else if (aspect == "9:16")
        ratio = 9. / 16;
    else if (aspect == "1:1")
        ratio = 1.;
    else
        return source;
    int width = source.width(), height = qMax(2, int(width / ratio) & ~1);
    if (height > source.height()) {
        height = source.height() & ~1;
        width = qMax(2, int(height * ratio) & ~1);
    }
    return {width, height};
}
static void remuxTrim(const QString &source, const QString &output, const QString &format,
                      const TrimBounds &bounds, std::atomic_bool *cancel,
                      std::function<void(double)> progress) {
    AVFormatContext *input = nullptr;
    check(avformat_open_input(&input, source.toUtf8().constData(), nullptr, nullptr),
          "Open fast trim source");
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> inputOwner(
        input, [](auto *p) { avformat_close_input(&p); });
    check(avformat_find_stream_info(input, nullptr), "Read fast trim streams");
    AVFormatContext *out = nullptr;
    check(avformat_alloc_output_context2(&out, nullptr, format.toUtf8().constData(),
                                         output.toUtf8().constData()),
          "Create fast trim output");
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> outputOwner(out, [](auto *p) {
        if (p->pb)
            avio_closep(&p->pb);
        avformat_free_context(p);
    });
    out->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
    std::vector<int> mapping(input->nb_streams, -1);
    std::vector<bool> videoStarted(input->nb_streams, false);
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        auto *stream = input->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        auto *copy = avformat_new_stream(out, nullptr);
        if (!copy)
            throw std::bad_alloc();
        mapping[i] = copy->index;
        check(avcodec_parameters_copy(copy->codecpar, stream->codecpar), "Copy encoded trim stream");
        copy->codecpar->codec_tag = 0;
        copy->time_base = stream->time_base;
        copy->avg_frame_rate = stream->avg_frame_rate;
        copy->sample_aspect_ratio = stream->sample_aspect_ratio;
        av_dict_copy(&copy->metadata, stream->metadata, 0);
        copy->disposition = stream->disposition;
    }
    av_dict_copy(&out->metadata, input->metadata, 0);
    check(avio_open(&out->pb, output.toUtf8().constData(), AVIO_FLAG_WRITE), "Open fast trim output file");
    check(avformat_write_header(out, nullptr), "Write fast trim header");
    auto *packet = av_packet_alloc();
    if (!packet)
        throw std::bad_alloc();
    std::unique_ptr<AVPacket, void (*)(AVPacket *)> packetOwner(packet, [](auto *p) { av_packet_free(&p); });
    int result;
    while ((result = av_read_frame(input, packet)) >= 0) {
        if (cancel && cancel->load())
            throw std::runtime_error("Cancelled");
        int index = packet->stream_index;
        if (mapping[size_t(index)] < 0) {
            av_packet_unref(packet);
            continue;
        }
        auto *stream = input->streams[index];
        auto *dest = out->streams[mapping[size_t(index)]];
        auto timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        const double origin =
            input->start_time == AV_NOPTS_VALUE ? 0. : double(input->start_time) / AV_TIME_BASE;
        double time = timestamp * av_q2d(stream->time_base) - origin;
        double duration = packet->duration * av_q2d(stream->time_base);
        bool video = stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        if (video && !videoStarted[size_t(index)] && (packet->flags & AV_PKT_FLAG_KEY) &&
            time >= bounds.start - 1e-6)
            videoStarted[size_t(index)] = true;
        bool retain =
            video ? (videoStarted[size_t(index)] && time >= bounds.start - 1e-6 && time < bounds.end - 1e-6)
                  : (time + duration > bounds.start + 1e-6 && time < bounds.end - 1e-6);
        if (!retain) {
            av_packet_unref(packet);
            continue;
        }
        const auto shift = qRound64((bounds.start + origin) / av_q2d(stream->time_base));
        if (packet->pts != AV_NOPTS_VALUE)
            packet->pts -= shift;
        if (packet->dts != AV_NOPTS_VALUE)
            packet->dts -= shift;
        av_packet_rescale_ts(packet, stream->time_base, dest->time_base);
        packet->stream_index = dest->index;
        packet->pos = -1;
        check(av_interleaved_write_frame(out, packet), "Write copied trim packet");
        av_packet_unref(packet);
        if (progress)
            progress(qBound(0., (time - bounds.start) / (bounds.end - bounds.start), .99));
    }
    if (result != AVERROR_EOF)
        check(result, "Read fast trim packets");
    check(av_write_trailer(out), "Finalize fast trim output");
    check(avio_closep(&out->pb), "Close fast trim output");
}
MediaInfo editMedia(const QString &source, const QString &output, const EditRequest &r,
                    const QString &encoder, const QString &quality, std::atomic_bool *cancel,
                    std::function<void(double)> progress) {
    const auto original = inspect(source);
    auto bounds = trimBounds(source, r.start, r.end, r.fast);
    const auto *codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (!r.fast && (!codec || codec->id != original.codecId))
        throw std::runtime_error("Editing requires a verified implementation of the source codec");
    bool audioChanged = r.mixAudio || r.tracks.size() != original.audioTracks;
    for (auto gain : r.gains)
        audioChanged |= std::abs(gain - 1.) > 1e-9;
    if (r.fast && (r.crop || r.aspect != "Source" || audioChanged))
        throw std::runtime_error(
            "Fast packet-copy trim cannot also crop or change audio. Choose Exact trim.");
    QString ext = QFileInfo(output).suffix().toLower();
    QString format = ext == "mkv" ? "matroska" : ext;
    if (!QStringList{"mp4", "mkv", "webm", "mov"}.contains(ext))
        throw std::runtime_error("Unsupported edit container");
    QString partial = output + ".partial";
    struct Cleanup {
        QString path;
        ~Cleanup() {
            QFile::remove(path);
        }
    } cleanup{partial};
    QStringList args{"-v", "error", "-y"};
    args << "-ss" << QString::number(bounds.start, 'f', 9);
    args << "-i" << source;
    args << "-t" << QString::number(bounds.end - bounds.start, 'f', 9) << "-map" << "0:v:0";
    if (r.fast) {
        args << "-map" << "0:a?" << "-c" << "copy" << "-avoid_negative_ts" << "make_zero";
    } else {
        args << encoderArguments(encoder, quality);
        QStringList filters;
        if (r.crop) {
            QRect region = r.region;
            region.setX(region.x() & ~1);
            region.setY(region.y() & ~1);
            region.setWidth(region.width() & ~1);
            region.setHeight(region.height() & ~1);
            if (region.width() < 2 || region.height() < 2 ||
                !QRect(0, 0, original.width, original.height).contains(region))
                throw std::runtime_error("Choose a visible crop region inside the source image");
            filters << QString("crop=%1:%2:%3:%4")
                           .arg(region.width())
                           .arg(region.height())
                           .arg(region.x())
                           .arg(region.y());
        }
        QSize dimensions = aspectSize(QSize(original.width, original.height), r.aspect);
        if (r.aspect != "Source") {
            if (!r.fit && !r.crop)
                throw std::runtime_error("Destructive aspect conversion requires a selected crop region");
            filters << QString("scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=%1:"
                               "%2:(ow-iw)/2:(oh-ih)/2,setsar=1")
                           .arg(dimensions.width())
                           .arg(dimensions.height());
        }
        if (!filters.isEmpty())
            args << "-vf" << filters.join(',');
        QStringList audioFilters;
        QStringList labels;
        for (int index = 0; index < r.tracks.size(); ++index) {
            int track = r.tracks[index];
            if (track < 0 || track >= original.audioTracks)
                throw std::runtime_error("Selected audio track no longer exists");
            double gain = index < r.gains.size() ? r.gains[index] : 1.;
            if (!std::isfinite(gain) || gain < 0 || gain > 4)
                throw std::runtime_error("Invalid audio gain");
            QString label = "a" + QString::number(index);
            audioFilters << QString("[0:a:%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS,volume=%4[%5]")
                                .arg(track)
                                .arg(0., 0, 'f', 9)
                                .arg(bounds.end - bounds.start, 0, 'f', 9)
                                .arg(gain, 0, 'f', 6)
                                .arg(label);
            labels << '[' + label + ']';
        }
        if (!labels.isEmpty()) {
            if (r.mixAudio) {
                audioFilters
                    << labels.join("") +
                           QString(
                               "amix=inputs=%1:normalize=0:duration=longest,alimiter=limit=0.98:level=0[mix]")
                               .arg(labels.size());
                args << "-filter_complex" << audioFilters.join(';') << "-map" << "[mix]";
            } else {
                args << "-filter_complex" << audioFilters.join(';');
                for (auto label : labels)
                    args << "-map" << label;
            }
            args << "-c:a" << (ext == "webm" ? "libopus" : ext == "mkv" ? "flac" : "aac");
        } else
            args << "-an";
        if (original.hdr)
            args << "-color_primaries" << original.primaries << "-color_trc" << original.transfer
                 << "-colorspace" << original.matrix << "-color_range" << QString::number(original.range);
    }
    args << "-map_metadata" << "0" << "-f" << format << partial;
    if (r.fast)
        remuxTrim(source, partial, format, bounds, cancel, progress);
    else {
        auto process = mediaProcess(args, bounds.end - bounds.start, cancel, progress);
        if (process.code != 0)
            throw std::runtime_error(process.cancelled ? "Cancelled"
                                                       : QString::fromUtf8(process.output).toStdString());
    }
    if (!decodeValidate(partial, cancel))
        throw std::runtime_error("Edited file failed complete decoding validation");
    auto edited = inspect(partial);
    double tolerance = 1. / qMax(1., edited.fps) + edited.audioRounding + 0.001;
    if (std::abs(edited.duration - (bounds.end - bounds.start)) > tolerance)
        throw std::runtime_error("Edited file duration did not match the disclosed trim boundaries");
    if (edited.codecId != original.codecId)
        throw std::runtime_error("Edited file codec changed unexpectedly");
    if (original.hdr &&
        (!edited.hdr || edited.primaries != original.primaries || edited.transfer != original.transfer))
        throw std::runtime_error("HDR colour properties were not preserved");
    if (!QFile::rename(partial, output))
        throw std::runtime_error("Cannot finalize edited output");
    return edited;
}
} // namespace clip
