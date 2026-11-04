#include "hdr.h"
#include <array>
#include <bit>
#include <cmath>
namespace clip {
float halfFloat(quint16 h) {
    const quint32 sign = quint32(h & 0x8000) << 16;
    quint32 exponent = (h >> 10) & 31, mantissa = h & 1023, bits;
    if (exponent == 0) {
        if (!mantissa)
            bits = sign;
        else {
            int shift = 0;
            while (!(mantissa & 1024)) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 1023;
            bits = sign | (quint32(127 - 14 - shift) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31)
        bits = sign | 0x7f800000 | (mantissa << 13);
    else
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    return std::bit_cast<float>(bits);
}
double pqEncode(double nits) {
    constexpr double m1 = 2610. / 16384, m2 = 2523. / 32, c1 = 3424. / 4096, c2 = 2413. / 128,
                     c3 = 2392. / 128;
    double p = std::pow(qBound(0., nits, 10000.) / 10000., m1);
    return std::pow((c1 + c2 * p) / (1 + c3 * p), m2);
}
double pqDecode(double signal) {
    constexpr double m1 = 2610. / 16384, m2 = 2523. / 32, c1 = 3424. / 4096, c2 = 2413. / 128,
                     c3 = 2392. / 128;
    double p = std::pow(qBound(0., signal, 1.), 1 / m2);
    return 10000. * std::pow(qMax(0., p - c1) / (c2 - c3 * p), 1 / m1);
}
namespace {
struct Color {
    double r, g, b;
};
double fastPq(double nits) {
    static const auto table = [] {
        std::array<double, 65537> a{};
        for (size_t i = 0; i < a.size(); ++i) {
            double x = double(i) / 65536.;
            a[i] = pqEncode(x * x * 10000.);
        }
        return a;
    }();
    double index = std::sqrt(qBound(0., nits, 10000.) / 10000.) * 65536.;
    size_t low = qMin(size_t(index), size_t(65535));
    double f = index - double(low);
    return table[low] * (1 - f) + table[low + 1] * f;
}
Color pixel(const CapturedFrame &s, int x, int y) {
    const auto *p = reinterpret_cast<const quint16 *>(s.pixels.constData() + qsizetype(y) * s.stride) + x * 4;
    return {halfFloat(p[0]), halfFloat(p[1]), halfFloat(p[2])};
}
Color sample(const CapturedFrame &s, int x, int y, int width, int height) {
    if (s.width == width && s.height == height)
        return pixel(s, x, y);
    double sx = qBound(0., (x + .5) * s.width / width - .5, double(s.width - 1));
    double sy = qBound(0., (y + .5) * s.height / height - .5, double(s.height - 1));
    int ix = int(sx), iy = int(sy);
    double fx = sx - ix, fy = sy - iy;
    auto a = pixel(s, ix, iy), b = pixel(s, qMin(ix + 1, s.width - 1), iy),
         c = pixel(s, ix, qMin(iy + 1, s.height - 1)),
         d = pixel(s, qMin(ix + 1, s.width - 1), qMin(iy + 1, s.height - 1));
    return {(a.r * (1 - fx) + b.r * fx) * (1 - fy) + (c.r * (1 - fx) + d.r * fx) * fy,
            (a.g * (1 - fx) + b.g * fx) * (1 - fy) + (c.g * (1 - fx) + d.g * fx) * fy,
            (a.b * (1 - fx) + b.b * fx) * (1 - fy) + (c.b * (1 - fx) + d.b * fx) * fy};
}
double hable(double v) {
    constexpr double a = .15, b = .50, c = .10, d = .20, e = .02, f = .30;
    return ((v * (a * v + c * b) + d * e) / (v * (a * v + b) + d * f)) - e / f;
}
double bt709(double v) {
    v = qBound(0., v, 1.);
    return v < .018 ? 4.5 * v : 1.099 * std::pow(v, .45) - .099;
}
Color transfer(Color c, bool hdr) {
    if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b))
        throw std::runtime_error("Capture surface contains non-finite HDR values");
    if (hdr) {
        Color rgb{.627404 * c.r + .329283 * c.g + .043313 * c.b,
                  .069097 * c.r + .919540 * c.g + .011362 * c.b,
                  .016391 * c.r + .088013 * c.g + .895595 * c.b};
        return {fastPq(rgb.r * 80.), fastPq(rgb.g * 80.), fastPq(rgb.b * 80.)};
    }
    double luma = qMax(0., .2126 * c.r + .7152 * c.g + .0722 * c.b) * .8;
    double mapped = hable(luma) / hable(10.);
    double gain = luma > 1e-8 ? mapped / luma : 0;
    return {bt709(c.r * .8 * gain), bt709(c.g * .8 * gain), bt709(c.b * .8 * gain)};
}
} // namespace
void convertFloatFrame(const CapturedFrame &source, AVFrame *f, bool hdr) {
    if (!source.floating || source.width <= 0 || source.height <= 0)
        throw std::runtime_error("Expected a floating-point capture surface");
    const auto format = AVPixelFormat(f->format);
    const auto *desc = av_pix_fmt_desc_get(format);
    bool p010 = format == AV_PIX_FMT_P010LE, nv12 = format == AV_PIX_FMT_NV12;
    bool supported = p010 || nv12 || format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV420P10LE ||
                     format == AV_PIX_FMT_YUV422P10LE || format == AV_PIX_FMT_YUV444P10LE ||
                     format == AV_PIX_FMT_YUV422P || format == AV_PIX_FMT_YUV444P;
    if (!supported || !desc)
        throw std::runtime_error("Selected encoder pixel format cannot preserve this float capture pipeline");
    int depth = desc->comp[0].depth;
    if (hdr && depth < 10)
        throw std::runtime_error("HDR requires a verified encoder output of at least 10 bits");
    int factor = 1 << (depth - 8);
    double kr = hdr ? .2627 : .2126, kb = hdr ? .0593 : .0722, kg = 1 - kr - kb;
    auto write = [&](int plane, int x, int y, double value) {
        int code = qBound(0, qRound(value), ((1 << depth) - 1));
        auto *row = f->data[plane] + qsizetype(y) * f->linesize[plane];
        if (depth > 8)
            reinterpret_cast<quint16 *>(row)[x] = quint16(p010 ? code << 6 : code);
        else
            row[x] = quint8(code);
    };
    const int chromaWidth = 1 << desc->log2_chroma_w, chromaHeight = 1 << desc->log2_chroma_h;
    std::array<std::vector<Color>, 2> rows;
    for (auto &row : rows)
        row.resize(size_t(f->width));
    for (int y = 0; y < f->height; y += chromaHeight) {
        for (int dy = 0; dy < chromaHeight; ++dy)
            for (int x = 0; x < f->width; ++x) {
                auto c = transfer(sample(source, x, y + dy, f->width, f->height), hdr);
                rows[size_t(dy)][size_t(x)] = c;
                double luma = kr * c.r + kg * c.g + kb * c.b;
                write(0, x, y + dy, (16 + 219 * luma) * factor);
            }
        for (int x = 0; x < f->width; x += chromaWidth) {
            Color c{};
            for (int dy = 0; dy < chromaHeight; ++dy)
                for (int dx = 0; dx < chromaWidth; ++dx) {
                    auto p = rows[size_t(dy)][size_t(x + dx)];
                    c.r += p.r;
                    c.g += p.g;
                    c.b += p.b;
                }
            double area = chromaWidth * chromaHeight;
            c.r /= area;
            c.g /= area;
            c.b /= area;
            double luma = kr * c.r + kg * c.g + kb * c.b;
            double u = (128 + 224 * (c.b - luma) / (2 * (1 - kb))) * factor,
                   v = (128 + 224 * (c.r - luma) / (2 * (1 - kr))) * factor;
            int cx = x / chromaWidth, cy = y / chromaHeight;
            if (p010 || nv12) {
                write(1, cx * 2, cy, u);
                write(1, cx * 2 + 1, cy, v);
            } else {
                write(1, cx, cy, u);
                write(2, cx, cy, v);
            }
        }
    }
    f->color_primaries = hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
    f->color_trc = hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
    f->colorspace = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    f->color_range = AVCOL_RANGE_MPEG;
}
QString sdrToneMapFilter() {
    return "zscale=t=linear:npl=100,format=gbrpf32le,tonemap=tonemap=hable:desat=0,zscale=p=bt709:t=bt709:m="
           "bt709:r=limited,sidedata=mode=delete:type=MASTERING_DISPLAY_METADATA,sidedata=mode=delete:type="
           "CONTENT_LIGHT_LEVEL,sidedata=mode=delete:type=DYNAMIC_HDR_PLUS,sidedata=mode=delete:type=DOVI_"
           "RPU_BUFFER,sidedata=mode=delete:type=DOVI_METADATA,format=yuv420p";
}
QStringList colorArguments(const MediaInfo &m, bool sdr) {
    if (sdr)
        return {"-color_primaries", "bt709", "-color_trc", "bt709",  "-colorspace", "bt709",
                "-color_range",     "tv",    "-pix_fmt",   "yuv420p"};
    QStringList args;
    if (m.primaries != "unknown" && !m.primaries.isEmpty())
        args << "-color_primaries" << m.primaries;
    if (m.transfer != "unknown" && !m.transfer.isEmpty())
        args << "-color_trc" << m.transfer;
    if (m.matrix != "unknown" && !m.matrix.isEmpty())
        args << "-colorspace" << m.matrix;
    args << "-color_range" << QString::number(m.range);
    if (m.hdr) {
        auto pix = av_get_pix_fmt(m.pixelFormat.toUtf8().constData());
        auto *desc = av_pix_fmt_desc_get(pix);
        if (!desc || desc->comp[0].depth < 10)
            throw std::runtime_error("HDR source does not expose a supported high-bit-depth pixel format");
        args << "-pix_fmt" << m.pixelFormat;
    }
    return args;
}
} // namespace clip
