#include "media.h"
namespace clip {
QJsonObject MediaInfo::json() const {
    return {{"duration", duration},
            {"fps", fps},
            {"width", width},
            {"height", height},
            {"codec", codec},
            {"container", container},
            {"pixelFormat", pixelFormat},
            {"audioTracks", audioTracks},
            {"audioBytes", audioBytes},
            {"bytes", bytes},
            {"hdr", hdr},
            {"transfer", transfer},
            {"primaries", primaries},
            {"matrix", matrix},
            {"range", range}};
}
MediaInfo inspect(const QString &path, bool countAudio) {
    AVFormatContext *raw = nullptr;
    check(avformat_open_input(&raw, path.toUtf8().constData(), nullptr, nullptr), "Open media");
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx(
        raw, [](auto *p) { avformat_close_input(&p); });
    check(avformat_find_stream_info(raw, nullptr), "Read streams");
    MediaInfo m;
    m.bytes = QFileInfo(path).size();
    m.duration = raw->duration == AV_NOPTS_VALUE ? 0 : double(raw->duration) / AV_TIME_BASE;
    m.container = QString::fromUtf8(raw->iformat->name);
    for (unsigned i = 0; i < raw->nb_streams; ++i) {
        auto s = raw->streams[i];
        auto p = s->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++m.audioTracks;
            if (p->sample_rate > 0)
                m.audioRounding = qMax(m.audioRounding, double(p->frame_size) / p->sample_rate);
        }
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && m.videoStream < 0) {
            m.videoStream = int(i);
            m.width = p->width;
            m.height = p->height;
            m.codecId = p->codec_id;
            m.codec = avcodec_get_name(p->codec_id);
            m.fps = av_q2d(av_guess_frame_rate(raw, s, nullptr));
            auto pix = av_get_pix_fmt_name(AVPixelFormat(p->format));
            m.pixelFormat = pix ? pix : "unknown";
            auto tr = av_color_transfer_name(p->color_trc), pr = av_color_primaries_name(p->color_primaries),
                 cs = av_color_space_name(p->color_space);
            m.transfer = tr ? tr : "unknown";
            m.primaries = pr ? pr : "unknown";
            m.matrix = cs ? cs : "unknown";
            m.range = p->color_range;
            m.hdr = p->color_trc == AVCOL_TRC_SMPTE2084 || p->color_trc == AVCOL_TRC_ARIB_STD_B67;
            if (m.duration <= 0 && s->duration != AV_NOPTS_VALUE)
                m.duration = s->duration * av_q2d(s->time_base);
        }
    }
    if (m.videoStream < 0)
        throw std::runtime_error("No video stream");
    if (countAudio) {
        auto pkt = av_packet_alloc();
        while (av_read_frame(raw, pkt) >= 0) {
            if (raw->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                m.audioBytes += pkt->size;
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
    return m;
}
QImage thumbnail(const QString &path, double sec) {
    AVFormatContext *f = nullptr;
    if (avformat_open_input(&f, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return {};
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> fmt(
        f, [](auto *p) { avformat_close_input(&p); });
    if (avformat_find_stream_info(f, nullptr) < 0)
        return {};
    int stream = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0)
        return {};
    auto s = f->streams[stream];
    auto codec = avcodec_find_decoder(s->codecpar->codec_id);
    if (!codec)
        return {};
    auto c = avcodec_alloc_context3(codec);
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> ctx(
        c, [](auto *p) { avcodec_free_context(&p); });
    avcodec_parameters_to_context(c, s->codecpar);
    c->thread_count = 2;
    if (avcodec_open2(c, codec, nullptr) < 0)
        return {};
    if (sec > 0)
        av_seek_frame(f, stream, qint64(sec / av_q2d(s->time_base)), AVSEEK_FLAG_BACKWARD);
    auto p = av_packet_alloc();
    auto frame = av_frame_alloc();
    QImage out;
    while (av_read_frame(f, p) >= 0) {
        if (p->stream_index == stream && avcodec_send_packet(c, p) >= 0 &&
            avcodec_receive_frame(c, frame) >= 0) {
            int w = 320, h = qMax(2, frame->height * w / frame->width);
            out = QImage(w, h, QImage::Format_RGBA8888);
            auto sw = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), w, h,
                                     AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sw) {
                uint8_t *data[] = {out.bits()};
                int stride[] = {int(out.bytesPerLine())};
                sws_scale(sw, frame->data, frame->linesize, 0, frame->height, data, stride);
                sws_freeContext(sw);
            }
            av_packet_unref(p);
            break;
        }
        av_packet_unref(p);
    }
    av_frame_free(&frame);
    av_packet_free(&p);
    return out;
}
bool decodeValidate(const QString &path, std::atomic_bool *cancel) {
    return runProcess(
               ffmpegPath(),
               {"-v", "error", "-xerror", "-i", path, "-map", "0:v", "-map", "0:a?", "-f", "null", "-"}, 0,
               cancel)
               .code == 0;
}
void configureEncoder(AVCodecContext *c, const QString &quality, qint64 bitrate, bool offline) {
    const QString n = c->codec->name;
    int tier = QStringList{"Maximum", "High", "Balanced", "Compact"}.indexOf(quality);
    if (tier < 0)
        tier = 1;
    c->bit_rate = bitrate;
    c->thread_count = offline ? 0 : 4;
    auto opt = [&](const char *key, const QString &value) {
        return av_opt_set(c->priv_data, key, value.toUtf8().constData(), 0);
    };
    if (n.contains("nvenc")) {
        opt("preset", offline ? "p7" : "p4");
        opt("tune", offline ? "hq" : "ll");
        if (!bitrate) {
            opt("rc", "vbr");
            opt("cq", QString::number(16 + tier * 4));
        }
    } else if (n.contains("libx264") || n.contains("libx265")) {
        opt("preset", offline ? "slow" : "veryfast");
        if (!bitrate)
            opt("crf", QString::number(16 + tier * 4));
    } else if (n.contains("_amf")) {
        opt("quality", offline ? "quality" : "speed");
        if (!bitrate) {
            opt("rc", "cqp");
            opt("qp_i", QString::number(16 + tier * 4));
            opt("qp_p", QString::number(18 + tier * 4));
        }
    } else if (n.contains("_qsv")) {
        opt("preset", offline ? "slow" : "veryfast");
        if (!bitrate)
            c->global_quality = 16 + tier * 4;
    } else if (n.contains("libvpx")) {
        opt("deadline", offline ? "good" : "realtime");
        opt("cpu-used", offline ? "2" : "6");
        if (!bitrate) {
            opt("crf", QString::number(20 + tier * 5));
            c->bit_rate = 0;
        }
    } else if (n.contains("libaom")) {
        opt("cpu-used", offline ? "4" : "8");
        if (!bitrate)
            opt("crf", QString::number(20 + tier * 5));
    } else if (n.contains("libsvtav1")) {
        opt("preset", offline ? "6" : "10");
        if (!bitrate)
            opt("crf", QString::number(20 + tier * 5));
    } else if (n.startsWith("prores")) {
        opt("profile", "3");
    } else if (n == "ffv1") {
        opt("level", "3");
        opt("coder", "1");
        opt("context", "1");
    } else if (!bitrate) {
        const auto *desc = avcodec_descriptor_get(c->codec_id);
        if (!desc || !(desc->props & AV_CODEC_PROP_LOSSLESS)) {
            c->flags |= AV_CODEC_FLAG_QSCALE;
            c->global_quality = (2 + tier * 3) * FF_QP2LAMBDA;
        }
    }
}
} // namespace clip
