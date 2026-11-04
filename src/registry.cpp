#include "registry.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
extern "C" {
#include <libavcodec/codec_desc.h>
#include <libavutil/cpu.h>
}
using Microsoft::WRL::ComPtr;
namespace clip {
QJsonObject Adapter::json() const {
    return {{"name", name},
            {"driver", driver},
            {"luid", luid},
            {"vendor", qint64(vendor)},
            {"device", qint64(device)},
            {"index", index}};
}
QJsonObject Display::json() const {
    return {{"name", name},     {"width", width},     {"height", height}, {"adapter", adapter},
            {"output", output}, {"refresh", refresh}, {"hdr", hdr}};
}
bool EncoderCaps::replaySuitable() const {
    static const QStringList raw{"avrp", "avui", "bitpacked", "r10k", "r210", "v210", "y41p", "yuv4", "vbn"};
    return avcodec_find_decoder(AVCodecID(codecId)) && !raw.contains(name) &&
           !description.contains("uncompressed", Qt::CaseInsensitive);
}
QJsonObject EncoderCaps::json() const {
    return {{"name", name},
            {"codec", codec},
            {"description", description},
            {"backend", backend},
            {"reason", reason},
            {"codecId", codecId},
            {"pixelFormats", QJsonArray::fromStringList(pixelFormats)},
            {"rateControls", QJsonArray::fromStringList(rateControls)},
            {"containers", QJsonArray::fromStringList(containers)},
            {"tested", tested},
            {"included", included},
            {"hardware", hardware},
            {"hardwareMatched", hardwareMatched},
            {"initialized", initialized},
            {"encoded", encoded},
            {"replaySuitable", replaySuitable()},
            {"appropriateForLive", replaySuitable() && live},
            {"appropriateForExport", encoded},
            {"live", live},
            {"hdr", hdr},
            {"elapsedMs", elapsedMs}};
}
EncoderCaps EncoderCaps::fromJson(const QJsonObject &o) {
    EncoderCaps c;
    c.name = o["name"].toString();
    c.codec = o["codec"].toString();
    c.description = o["description"].toString();
    c.backend = o["backend"].toString();
    c.reason = o["reason"].toString();
    c.codecId = o["codecId"].toInt();
    for (auto v : o["pixelFormats"].toArray())
        c.pixelFormats << v.toString();
    for (auto v : o["rateControls"].toArray())
        c.rateControls << v.toString();
    for (auto v : o["containers"].toArray())
        c.containers << v.toString();
    c.tested = o["tested"].toArray();
    c.hardware = o["hardware"].toBool();
    c.hardwareMatched = o["hardwareMatched"].toBool();
    c.initialized = o["initialized"].toBool();
    c.encoded = o["encoded"].toBool();
    c.live = o["live"].toBool();
    c.hdr = o["hdr"].toBool();
    c.elapsedMs = o["elapsedMs"].toDouble();
    return c;
}
static bool imageCodec(const AVCodec *c) {
    // Image and subtitle descriptors are not ordinary replay streams, even if their
    // muxers can technically store an image sequence. Intra-only *video* remains.
    const auto *d = avcodec_descriptor_get(c->id);
    if (!d)
        return true;
    static const QStringList images{"alias_pix", "dpx",     "exr",  "fits",   "hdr",  "jpegls", "ljpeg",
                                    "pbm",       "pfm",     "pgm",  "pgmyuv", "phm",  "ppm",    "qoi",
                                    "sgi",       "sunrast", "wbmp", "xface",  "vnull"};
    if (images.contains(QString::fromUtf8(c->name)))
        return true;
    return (d->props & AV_CODEC_PROP_TEXT_SUB) || d->mime_types != nullptr || c->id == AV_CODEC_ID_GIF ||
           c->id == AV_CODEC_ID_APNG || c->id == AV_CODEC_ID_RAWVIDEO || c->id == AV_CODEC_ID_WRAPPED_AVFRAME;
}
void Registry::discover() {
    adapters.clear();
    displays.clear();
    encoders.clear();
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            LARGE_INTEGER driver{};
            a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
            Adapter ga;
            ga.name = QString::fromWCharArray(d.Description);
            ga.vendor = d.VendorId;
            ga.device = d.DeviceId;
            ga.index = int(i);
            ga.luid = QString::number(d.AdapterLuid.HighPart, 16) + ":" +
                      QString::number(d.AdapterLuid.LowPart, 16);
            ga.driver = QString::number(driver.QuadPart);
            adapters << ga;
            for (UINT j = 0;; ++j) {
                ComPtr<IDXGIOutput> o;
                if (a->EnumOutputs(j, &o) == DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_OUTPUT_DESC od{};
                o->GetDesc(&od);
                if (!od.AttachedToDesktop)
                    continue;
                Display m;
                m.name = QString::fromWCharArray(od.DeviceName);
                m.handle = quintptr(od.Monitor);
                m.adapter = int(i);
                m.output = int(j);
                m.width = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
                m.height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
                DEVMODEW dm{};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
                    m.refresh = dm.dmDisplayFrequency;
                ComPtr<IDXGIOutput6> o6;
                if (SUCCEEDED(o.As(&o6))) {
                    DXGI_OUTPUT_DESC1 dd{};
                    if (SUCCEEDED(o6->GetDesc1(&dd)))
                        m.hdr = dd.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                }
                displays << m;
            }
        }
    void *it = nullptr;
    while (const AVCodec *c = av_codec_iterate(&it)) {
        if (!av_codec_is_encoder(c) || c->type != AVMEDIA_TYPE_VIDEO || imageCodec(c))
            continue;
        EncoderCaps e;
        e.name = c->name;
        e.codec = avcodec_get_name(c->id);
        e.description = c->long_name ? c->long_name : c->name;
        e.codecId = c->id;
        e.hardware = bool(c->capabilities & AV_CODEC_CAP_HARDWARE) || e.name.contains("_qsv") ||
                     e.name.contains("_amf") || e.name.contains("_nvenc");
        if (e.name.contains("nvenc"))
            e.backend = "NVIDIA NVENC";
        else if (e.name.contains("amf"))
            e.backend = "AMD AMF";
        else if (e.name.contains("qsv"))
            e.backend = "Intel QSV";
        else if (e.hardware)
            e.backend = "Hardware";
        for (const auto &a : adapters)
            if ((e.backend == "NVIDIA NVENC" && a.vendor == 0x10de) ||
                (e.backend == "AMD AMF" && a.vendor == 0x1002) ||
                (e.backend == "Intel QSV" && a.vendor == 0x8086))
                e.hardwareMatched = true;
        const void *cfg = nullptr;
        int count = 0;
        if (avcodec_get_supported_config(nullptr, c, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &count) >= 0 &&
            cfg) {
            auto p = static_cast<const AVPixelFormat *>(cfg);
            for (int n = 0; n < count; ++n) {
                auto name = av_get_pix_fmt_name(p[n]);
                if (name)
                    e.pixelFormats << name;
            }
        }
        for (auto name : {"mp4", "matroska", "webm", "mov"}) {
            auto f = av_guess_format(name, nullptr, nullptr);
            if (f && avformat_query_codec(f, c->id, FF_COMPLIANCE_NORMAL) > 0)
                e.containers << (QString(name) == "matroska" ? "mkv" : name);
        }
        // Query private options rather than suggesting unsupported generic controls.
        auto ctx = avcodec_alloc_context3(c);
        if (ctx) {
            if (ctx->priv_data) {
                const AVOption *op = nullptr;
                while ((op = av_opt_next(ctx->priv_data, op))) {
                    QString key = op->name;
                    if (QStringList{"crf", "cq", "qp", "rc", "global_quality", "qscale", "lossless",
                                    "quality", "preset"}
                            .contains(key))
                        e.rateControls << key;
                }
            }
            avcodec_free_context(&ctx);
        }
        e.reason = "Included; not tested";
        encoders << e;
    }
    std::sort(encoders.begin(), encoders.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
}
QString Registry::cacheKey() const {
    QJsonArray a;
    for (const auto &g : adapters) {
        auto identity = g.json();
        identity.remove("luid"); // Boot-local identifiers do not invalidate unchanged driver tests.
        a << identity;
    }
    QByteArray bytes = QJsonDocument(a).toJson(QJsonDocument::Compact) + QByteArray(av_version_info()) +
                       avcodec_configuration() + qVersion() + QByteArray::number(av_get_cpu_flags()) +
                       "probe-v5-replay-classification";
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
void Registry::probe(std::function<void(QString)> progress, std::atomic_bool *cancel, bool useCache) {
    auto cache = readJson(stateDir() + "/encoders.json");
    if (useCache && cache["key"].toString() == cacheKey()) {
        recommended = cache["recommended"].toString();
        auto items = cache["encoders"].toArray();
        for (auto &e : encoders)
            for (auto v : items)
                if (v.toObject()["name"].toString() == e.name)
                    e = EncoderCaps::fromJson(v.toObject());
        return;
    }
    for (auto &e : encoders) {
        if (cancel && cancel->load())
            break;
        if (progress)
            progress(e.name);
        if (e.hardware && !e.hardwareMatched) {
            e.reason = "Matching hardware not detected; initialization not attempted";
            continue;
        }
        if (e.containers.isEmpty()) {
            e.reason = "No compatible application container reported by muxers";
            continue;
        }
        QString result = QDir::temp().filePath("clipper-probe-" +
                                               QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
        auto p = runProcess(QCoreApplication::applicationFilePath(), {"--probe", e.name, "--result", result},
                            15000, cancel);
        auto o = readJson(result);
        QFile::remove(result);
        e.initialized = o["initialized"].toBool();
        e.encoded = o["encoded"].toBool();
        e.elapsedMs = o["elapsedMs"].toDouble();
        e.live = false;
        e.reason = o["reason"].toString(p.timedOut ? "Probe timed out in isolated process"
                                                   : QString::fromUtf8(p.output.right(1500)));
        if (e.reason.isEmpty())
            e.reason = "Probe process failed";
        if (e.encoded) {
            e.tested.append(o);
            e.reason = "Synthetic SDR encode verified; capture calibration required for live recommendation";
            bool deep = false;
            for (const auto &name : e.pixelFormats) {
                auto *desc = av_pix_fmt_desc_get(av_get_pix_fmt(name.toUtf8().constData()));
                if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL) && desc->comp[0].depth >= 10)
                    deep = true;
            }
            if (deep && !(cancel && cancel->load())) {
                runProcess(QCoreApplication::applicationFilePath(),
                           {"--probe", e.name, "--hdr", "--result", result}, 20000, cancel);
                auto hdr = readJson(result);
                QFile::remove(result);
                e.hdr = hdr["encoded"].toBool() && hdr["decodedDepth"].toInt() >= 10;
                if (e.hdr)
                    e.tested.append(hdr);
            }
        }
    }
    QJsonArray a;
    for (const auto &e : encoders)
        a << e.json();
    writeJson(stateDir() + "/encoders.json", {{"key", cacheKey()}, {"encoders", a}});
}
QJsonObject Registry::testEncoder(const QString &name, bool hdr) {
    QJsonObject result{{"name", name}, {"initialized", false}, {"encoded", false},
                       {"width", 640}, {"height", 360},        {"fps", 60},
                       {"hdr", hdr}};
    QElapsedTimer time;
    time.start();
    try {
        const AVCodec *codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
        if (!codec)
            throw std::runtime_error("Encoder absent");
        auto c = avcodec_alloc_context3(codec);
        if (!c)
            throw std::bad_alloc();
        std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> ctx(
            c, [](auto *p) { avcodec_free_context(&p); });
        c->width = 640;
        c->height = 360;
        c->time_base = {1, 60};
        c->framerate = {60, 1};
        c->gop_size = 120;
        c->max_b_frames = 0;
        const void *cfg = nullptr;
        int count = 0;
        avcodec_get_supported_config(c, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &count);
        auto formats = static_cast<const AVPixelFormat *>(cfg);
        c->pix_fmt = AV_PIX_FMT_NONE;
        QList<AVPixelFormat> preferred =
            hdr ? QList<AVPixelFormat>{AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV422P10LE}
                : QList<AVPixelFormat>{AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUV422P10LE,
                                       AV_PIX_FMT_YUV444P, AV_PIX_FMT_BGRA};
        for (auto fmt : preferred) {
            for (int i = 0; i < count; ++i)
                if (formats[i] == fmt) {
                    c->pix_fmt = fmt;
                    break;
                }
            if (c->pix_fmt != AV_PIX_FMT_NONE)
                break;
        }
        if (c->pix_fmt == AV_PIX_FMT_NONE && count > 0)
            for (int i = 0; i < count; ++i) {
                auto d = av_pix_fmt_desc_get(formats[i]);
                if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
                    c->pix_fmt = formats[i];
                    break;
                }
            }
        if (c->pix_fmt == AV_PIX_FMT_NONE)
            c->pix_fmt = AV_PIX_FMT_YUV420P;
        if (hdr) {
            auto *desc = av_pix_fmt_desc_get(c->pix_fmt);
            if (!desc || desc->comp[0].depth < 10)
                throw std::runtime_error("No suitable high-bit-depth input format");
        }
        c->color_primaries = hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
        c->color_trc = hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
        c->colorspace = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
        c->color_range = AVCOL_RANGE_MPEG;
        configureEncoder(c, "High");
        check(avcodec_open2(c, codec, nullptr), "Initialize encoder");
        result["initialized"] = true;
        result["pixelFormat"] = QString(av_get_pix_fmt_name(c->pix_fmt));
        const auto *decoder = hdr ? avcodec_find_decoder(codec->id) : nullptr;
        std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> decodedContext(
            nullptr, [](auto *p) { avcodec_free_context(&p); });
        std::unique_ptr<AVFrame, void (*)(AVFrame *)> decodedFrame(nullptr,
                                                                   [](auto *p) { av_frame_free(&p); });
        if (hdr) {
            if (!decoder)
                throw std::runtime_error("No decoder to verify HDR bit depth");
            decodedContext.reset(avcodec_alloc_context3(decoder));
            decodedFrame.reset(av_frame_alloc());
            auto *params = avcodec_parameters_alloc();
            check(avcodec_parameters_from_context(params, c), "HDR test decoder parameters");
            check(avcodec_parameters_to_context(decodedContext.get(), params), "HDR test decoder context");
            avcodec_parameters_free(&params);
            decodedContext->thread_count = 2;
            check(avcodec_open2(decodedContext.get(), decoder, nullptr), "Open HDR verification decoder");
        }
        auto frame = av_frame_alloc();
        auto packet = av_packet_alloc();
        std::unique_ptr<AVFrame, void (*)(AVFrame *)> fr(frame, [](auto *p) { av_frame_free(&p); });
        std::unique_ptr<AVPacket, void (*)(AVPacket *)> pk(packet, [](auto *p) { av_packet_free(&p); });
        frame->format = c->pix_fmt;
        frame->width = c->width;
        frame->height = c->height;
        frame->color_primaries = c->color_primaries;
        frame->color_trc = c->color_trc;
        frame->colorspace = c->colorspace;
        frame->color_range = c->color_range;
        check(av_frame_get_buffer(frame, 32), "Allocate synthetic frame");
        int packets = 0;
        auto receive = [&] {
            int r;
            while ((r = avcodec_receive_packet(c, packet)) >= 0) {
                if (packet->size > 0)
                    ++packets;
                if (hdr) {
                    check(avcodec_send_packet(decodedContext.get(), packet), "Decode HDR test packet");
                    while (avcodec_receive_frame(decodedContext.get(), decodedFrame.get()) >= 0) {
                        auto *desc = av_pix_fmt_desc_get(AVPixelFormat(decodedFrame->format));
                        if (desc)
                            result["decodedDepth"] = desc->comp[0].depth;
                        av_frame_unref(decodedFrame.get());
                    }
                }
                av_packet_unref(packet);
            }
            if (r != AVERROR(EAGAIN) && r != AVERROR_EOF)
                check(r, "Receive test packet");
        };
        for (int i = 0; i < 12; ++i) {
            check(av_frame_make_writable(frame), "Writable frame");
            ptrdiff_t lines[4] = {frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                  frame->linesize[3]};
            check(av_image_fill_black(frame->data, lines, c->pix_fmt, AVCOL_RANGE_MPEG, c->width, c->height),
                  "Fill synthetic frame");
            frame->pts = i;
            check(avcodec_send_frame(c, frame), "Send test frame");
            receive();
        }
        check(avcodec_send_frame(c, nullptr), "Flush test frames");
        receive();
        result["packets"] = packets;
        if (hdr) {
            check(avcodec_send_packet(decodedContext.get(), nullptr), "Flush HDR test decoder");
            while (avcodec_receive_frame(decodedContext.get(), decodedFrame.get()) >= 0) {
                auto *desc = av_pix_fmt_desc_get(AVPixelFormat(decodedFrame->format));
                if (desc)
                    result["decodedDepth"] = desc->comp[0].depth;
                av_frame_unref(decodedFrame.get());
            }
        }
        result["encoded"] = packets > 0 && (!hdr || result["decodedDepth"].toInt() >= 10);
        result["reason"] =
            packets > 0 ? "Encoded and flushed synthetic frames" : "Encoder produced no packets";
    } catch (const std::exception &e) {
        result["reason"] = QString::fromUtf8(e.what());
    }
    result["elapsedMs"] = double(time.elapsed());
    return result;
}
const EncoderCaps *Registry::find(const QString &n) const {
    for (const auto &e : encoders)
        if (e.name == n)
            return &e;
    return nullptr;
}
QString Registry::automatic(bool hdr) const {
    if (hdr) {
        for (int codec : {AV_CODEC_ID_HEVC, AV_CODEC_ID_AV1, AV_CODEC_ID_H264})
            for (const auto &e : encoders)
                if (e.codecId == codec && e.encoded && e.hdr && e.live && e.hardware)
                    return e.name;
        for (const auto &e : encoders)
            if (e.encoded && e.hdr && e.live)
                return e.name;
        return {};
    }
    if (auto *e = find(recommended); e && e->encoded && e->live && e->codecId == AV_CODEC_ID_H264)
        return recommended;
    for (const auto &e : encoders)
        if (e.codecId == AV_CODEC_ID_H264 && e.hardware && e.encoded && e.live)
            return e.name;
    for (const auto &e : encoders)
        if (e.codecId == AV_CODEC_ID_H264 && e.encoded && e.live)
            return e.name;
    return {};
}
QString Registry::forCodec(int id, const QString &preferred, bool hdr) const {
    if (auto e = find(preferred); e && e->codecId == id && e->encoded && (!hdr || e->hdr))
        return e->name;
    for (const auto &e : encoders)
        if (e.codecId == id && e.encoded && !e.hardware && (!hdr || e.hdr))
            return e.name;
    for (const auto &e : encoders)
        if (e.codecId == id && e.encoded && (!hdr || e.hdr))
            return e.name;
    return {};
}
QJsonObject Registry::json() const {
    QJsonArray a, d, e;
    for (const auto &v : adapters)
        a << v.json();
    for (const auto &v : displays)
        d << v.json();
    for (const auto &v : encoders)
        e << v.json();
    return {
        {"key", cacheKey()}, {"recommended", recommended}, {"adapters", a}, {"displays", d}, {"encoders", e}};
}
} // namespace clip
