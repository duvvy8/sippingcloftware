#include "recorder.h"
#include "hdr.h"
#include "edit.h"
#include "replay_storage.h"
#include <winrt/base.h>
namespace clip {
namespace {
struct CodecDeleter {
    void operator()(AVCodecContext *p) const {
        avcodec_free_context(&p);
    }
};
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
struct SegmentWriter {
    AVFormatContext *format = nullptr;
    Codec video;
    std::vector<Codec> audio;
    std::vector<std::vector<float>> pending;
    std::vector<qint64> audioPts;
    std::vector<AVStream *> audioStreams;
    AVStream *videoStream = nullptr;
    SwsContext *scale = nullptr;
    QString path;
    int frames = 0, fps = 60, w = 0, h = 0;
    bool finalized = false;
    qint64 packetBytes = 0;
    std::function<void(qint64)> reserve;
    ~SegmentWriter() {
        if (scale)
            sws_freeContext(scale);
        if (format) {
            if (format->pb)
                avio_closep(&format->pb);
            avformat_free_context(format);
        }
        if (!finalized && !path.isEmpty())
            QFile::remove(path);
    }
    void open(const QString &p, const Settings &s, int width, int height, const QStringList &trackRoles,
              AVPixelFormat pixelFormat) {
        path = p;
        fps = s.fps;
        w = width;
        h = height;
        check(avformat_alloc_output_context2(&format, nullptr, "matroska", p.toUtf8().constData()),
              "Create replay segment");
        const auto *codec = avcodec_find_encoder_by_name(s.encoder.toUtf8().constData());
        if (!codec)
            throw std::runtime_error("Selected encoder is absent");
        video.reset(avcodec_alloc_context3(codec));
        auto *c = video.get();
        c->width = w;
        c->height = h;
        c->time_base = {1, fps};
        c->framerate = {fps, 1};
        c->gop_size = fps * 2;
        c->max_b_frames = 0;
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        c->pix_fmt = pixelFormat;
        c->color_primaries = s.hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
        c->color_trc = s.hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
        c->colorspace = s.hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
        c->color_range = AVCOL_RANGE_MPEG;
        configureEncoder(c, s.quality);
        check(avcodec_open2(c, codec, nullptr), "Open recording encoder");
        videoStream = avformat_new_stream(format, nullptr);
        if (!videoStream)
            throw std::bad_alloc();
        videoStream->time_base = c->time_base;
        videoStream->avg_frame_rate = c->framerate;
        check(avcodec_parameters_from_context(videoStream->codecpar, c), "Video parameters");
        for (int i = 0; i < trackRoles.size(); ++i) {
            auto ac = avcodec_find_encoder(AV_CODEC_ID_FLAC);
            if (!ac)
                throw std::runtime_error("FLAC buffer audio encoder missing");
            Codec a(avcodec_alloc_context3(ac));
            a->sample_rate = 48000;
            a->sample_fmt = AV_SAMPLE_FMT_S32;
            a->bits_per_raw_sample = 24;
            av_channel_layout_default(&a->ch_layout, 2);
            a->time_base = {1, 48000};
            a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            check(avcodec_open2(a.get(), ac, nullptr), "Open lossless replay audio encoder");
            auto stream = avformat_new_stream(format, nullptr);
            stream->time_base = a->time_base;
            check(avcodec_parameters_from_context(stream->codecpar, a.get()), "Audio parameters");
            av_dict_set(&stream->metadata, "title",
                        i == 0 ? "Compatibility mix" : qPrintable(QString("Source %1").arg(i)), 0);
            av_dict_set(&stream->metadata, "sipping_audio_role", qPrintable(trackRoles[i]), 0);
            audioStreams.push_back(stream);
            audio.push_back(std::move(a));
            pending.emplace_back();
            audioPts.push_back(0);
        }
        check(avio_open(&format->pb, p.toUtf8().constData(), AVIO_FLAG_WRITE), "Open replay segment file");
        check(avformat_write_header(format, nullptr), "Write replay header");
    }
    void packets(AVCodecContext *c, AVStream *s) {
        AVPacket *p = av_packet_alloc();
        if (!p)
            throw std::bad_alloc();
        std::unique_ptr<AVPacket, void (*)(AVPacket *)> owned(p, [](auto *v) { av_packet_free(&v); });
        int r;
        while ((r = avcodec_receive_packet(c, p)) >= 0) {
            packetBytes += p->size;
            if (reserve)
                reserve(packetBytes + 2 * 1024 * 1024);
            av_packet_rescale_ts(p, c->time_base, s->time_base);
            p->stream_index = s->index;
            check(av_interleaved_write_frame(format, p), "Write replay packet");
            av_packet_unref(p);
        }
        if (r != AVERROR(EAGAIN) && r != AVERROR_EOF)
            check(r, "Receive encoded replay packet");
    }
    void addVideo(const CapturedFrame &source) {
        auto f = av_frame_alloc();
        std::unique_ptr<AVFrame, void (*)(AVFrame *)> owned(f, [](auto *p) { av_frame_free(&p); });
        f->width = w;
        f->height = h;
        f->format = video->pix_fmt;
        f->pts = frames++;
        f->color_primaries = video->color_primaries;
        f->color_trc = video->color_trc;
        f->colorspace = video->colorspace;
        f->color_range = video->color_range;
        check(av_frame_get_buffer(f, 32), "Allocate recording frame");
        if (source.floating)
            convertFloatFrame(source, f, video->color_trc == AVCOL_TRC_SMPTE2084);
        else {
            scale = sws_getCachedContext(scale, source.width, source.height, AV_PIX_FMT_BGRA, w, h,
                                         video->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (!scale)
                throw std::runtime_error("Unsupported capture conversion");
            const uint8_t *src[] = {reinterpret_cast<const uint8_t *>(source.pixels.constData())};
            int stride[] = {source.stride};
            const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(scale, coeff, 1, coeff, 0, 0, 1 << 16, 1 << 16);
            check(sws_scale(scale, src, stride, 0, source.height, f->data, f->linesize),
                  "Convert capture frame");
        }
        check(avcodec_send_frame(video.get(), f), "Encode capture frame");
        packets(video.get(), videoStream);
    }
    void audioFrame(size_t i, int samples) {
        auto c = audio[i].get();
        auto f = av_frame_alloc();
        std::unique_ptr<AVFrame, void (*)(AVFrame *)> owned(f, [](auto *p) { av_frame_free(&p); });
        f->format = c->sample_fmt;
        f->sample_rate = 48000;
        f->nb_samples = samples;
        av_channel_layout_copy(&f->ch_layout, &c->ch_layout);
        f->pts = audioPts[i];
        audioPts[i] += samples;
        check(av_frame_get_buffer(f, 0), "Allocate replay audio frame");
        auto dest = reinterpret_cast<int32_t *>(f->data[0]);
        for (int n = 0; n < samples * 2; ++n)
            dest[n] = int32_t(qBound(-1., double(pending[i][n]), 1.) * 2147483392.);
        pending[i].erase(pending[i].begin(), pending[i].begin() + samples * 2);
        check(avcodec_send_frame(c, f), "Encode replay audio");
        packets(c, audioStreams[i]);
    }
    void addAudio(const QList<std::vector<float>> &blocks) {
        for (size_t i = 0; i < audio.size(); ++i) {
            pending[i].insert(pending[i].end(), blocks[qsizetype(i)].begin(), blocks[qsizetype(i)].end());
            int chunk = audio[i]->frame_size;
            while (pending[i].size() >= size_t(chunk) * 2)
                audioFrame(i, chunk);
        }
    }
    void finish() {
        if (finalized)
            return;
        check(avcodec_send_frame(video.get(), nullptr), "Flush delayed replay video");
        packets(video.get(), videoStream);
        for (size_t i = 0; i < audio.size(); ++i) {
            if (!pending[i].empty())
                audioFrame(i, int(pending[i].size() / 2));
            check(avcodec_send_frame(audio[i].get(), nullptr), "Flush replay audio");
            packets(audio[i].get(), audioStreams[i]);
        }
        check(av_write_trailer(format), "Finalize replay segment");
        check(avio_closep(&format->pb), "Close replay segment");
        QString final = path;
        final.chop(QString(".partial").size());
        if (!QFile::rename(path, final))
            throw std::runtime_error("Cannot finalize replay segment filename");
        path = final;
        finalized = true;
    }
};
struct CaptureQueue {
    std::mutex mutex;
    std::deque<CapturedFrame> frames;
    std::atomic_bool stop = false, ready = false;
    std::atomic<quint64> captured = 0, dropped = 0;
    QString error, backend;
};
} // namespace
QJsonObject RecordingStats::json() const {
    return {{"active", active},
            {"started", started},
            {"elapsed", elapsed},
            {"captured", qint64(captured)},
            {"encoded", qint64(encoded)},
            {"dropped", qint64(dropped)},
            {"audioDiscontinuities", qint64(audioDiscontinuities)},
            {"audioNotice", audioNotice},
            {"backlog", backlog},
            {"width", width},
            {"height", height},
            {"fps", fps},
            {"bytes", bytes},
            {"encoder", encoder},
            {"backend", backend},
            {"error", error}};
}
Recorder::~Recorder() {
    stop();
}
RecordingStats Recorder::stats() const {
    std::lock_guard lock(mutex_);
    auto s = stats_;
    s.active = active_;
    s.bytes = buffer_.bytes();
    if (s.active)
        s.elapsed = clockSeconds() - s.started;
    return s;
}
void Recorder::start(const Settings &s, const Registry &r) {
    stop();
    {
        std::lock_guard lock(mutex_);
        stats_ = {};
        initialized_ = false;
    }
    stop_ = false;
    stopAt_ = 0;
    active_ = true;
    worker_ = std::thread([this, s, r] { run(s, r); });
    std::unique_lock lock(mutex_);
    ready_.wait_for(lock, std::chrono::seconds(20), [this] { return initialized_; });
    if (!initialized_) {
        lock.unlock();
        stop();
        throw std::runtime_error("Capture initialization timed out");
    }
    if (!stats_.error.isEmpty()) {
        auto error = stats_.error;
        lock.unlock();
        stop();
        throw std::runtime_error(error.toStdString());
    }
}
void Recorder::stop() {
    stopAt_ = clockSeconds();
    stop_ = true;
    if (worker_.joinable())
        worker_.join();
    active_ = false;
}
void Recorder::run(Settings s, Registry registry) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    CaptureQueue queue;
    std::thread capture;
    AudioEngine audio;
    ReplayStorage storage;
    std::unique_ptr<SegmentWriter> writer;
    double segmentStart = 0;
    QList<FullscreenSpan> fullscreen;
    auto commit = [&] {
        if (writer && writer->frames) {
            int count = writer->frames;
            writer->finish();
            buffer_.add({writer->path, segmentStart, segmentStart + double(count) / s.fps,
                         QFileInfo(writer->path).size(), {}, std::exchange(fullscreen, {})});
            storage.committed(buffer_.segments());
            writer.reset();
        }
    };
    try {
        if (s.fps < 1 || s.fps > 240)
            throw std::runtime_error("Invalid recording frame rate");
        const auto *cap = registry.find(s.encoder);
        if (!cap || !cap->encoded || !cap->replaySuitable())
            throw std::runtime_error("Encoder has not passed an initialization and packet-output test");
        if (s.hdr && !cap->hdr)
            throw std::runtime_error("Selected encoder has not passed a 10-bit HDR encode and decode test");
        if (!cap->containers.contains("mkv"))
            throw std::runtime_error("Encoder cannot use independent replay segments in Matroska");
        if (s.monitor < 0 || s.monitor >= registry.displays.size())
            throw std::runtime_error("Selected capture display is unavailable");
        if (s.fps > registry.displays[s.monitor].refresh + 0.1)
            throw std::runtime_error("Recording frame rate exceeds selected display source rate");
        QDir().mkpath(s.bufferFolder);
        QStorageInfo initialDisk(s.bufferFolder);
        if (initialDisk.bytesAvailable() < 128LL * 1024 * 1024)
            throw std::runtime_error("Insufficient replay storage: at least 128 MiB free is required");
        audio.start(s.audio);
        const auto trackRoles = audio.trackRoles();
        capture = std::thread([&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try {
                Capture source;
                source.open(s, registry);
                {
                    std::lock_guard lock(queue.mutex);
                    queue.backend = source.backend();
                }
                queue.ready = true;
                const auto monitor = HMONITOR(registry.displays[s.monitor].handle);
                const auto windowCapture = s.source == "window" ? HWND(s.window) : nullptr;
                HWND lastFullscreen = nullptr;
                ApplicationIdentity fullscreenApp;
                while (!queue.stop) {
                    CapturedFrame frame;
                    if (source.next(frame)) {
                        const auto window = fullscreenCaptureWindow(monitor, windowCapture);
                        if (window != lastFullscreen) {
                            lastFullscreen = window;
                            fullscreenApp = windowApplication(quintptr(window));
                        }
                        frame.fullscreenApp = fullscreenApp;
                        std::lock_guard lock(queue.mutex);
                        ++queue.captured;
                        if (queue.frames.size() >= 16) {
                            queue.frames.pop_front();
                            ++queue.dropped;
                        }
                        queue.frames.push_back(std::move(frame));
                    } else
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } catch (const winrt::hresult_error &e) {
                std::lock_guard lock(queue.mutex);
                queue.error = QString::fromWCharArray(e.message().c_str());
                queue.ready = true;
            } catch (const std::exception &e) {
                std::lock_guard lock(queue.mutex);
                queue.error = QString::fromUtf8(e.what());
                queue.ready = true;
            }
            winrt::uninit_apartment();
        });
        QElapsedTimer wait;
        wait.start();
        CapturedFrame current;
        while (current.pixels.isEmpty() && !stop_) {
            {
                std::lock_guard lock(queue.mutex);
                if (!queue.error.isEmpty())
                    throw std::runtime_error(queue.error.toStdString());
                if (!queue.frames.empty()) {
                    current = std::move(queue.frames.front());
                    queue.frames.pop_front();
                }
            }
            if (wait.elapsed() > 8000)
                throw std::runtime_error("No capture frames arrived");
            if (current.pixels.isEmpty())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (stop_)
            throw std::runtime_error("Recording cancelled during initialization");
        if (s.height > current.height)
            throw std::runtime_error("Requested recording resolution exceeds the actual capture source");
        int h = s.height > 0 ? s.height : current.height;
        h -= h % 2;
        int w = (current.width * h / current.height) & ~1;
        // Check pins and retire the previous ring before recovery touches disk.
        // Startup I/O and codec initialization are outside the recorded timeline.
        buffer_.begin(0., s.retention, s.bufferBytes);
        storage.begin(s.bufferFolder, storageToken_);
        AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
        for (const auto &value : cap->tested) {
            const auto test = value.toObject();
            if (test["hdr"].toBool() == s.hdr && test.contains("pixelFormat") && test["encoded"].toBool()) {
                pixelFormat = av_get_pix_fmt(test["pixelFormat"].toString().toUtf8().constData());
                break;
            }
        }
        if (pixelFormat == AV_PIX_FMT_NONE)
            throw std::runtime_error("No verified pixel format matches the selected colour mode");
        const auto openWriter = [&] {
            QStorageInfo disk(s.bufferFolder);
            if (disk.bytesAvailable() < 128LL * 1024 * 1024)
                throw std::runtime_error(
                    "Replay disk is nearly full. Recording stopped; completed clips were preserved.");
            writer = std::make_unique<SegmentWriter>();
            writer->reserve = [this](qint64 bytes) { buffer_.reserve(bytes); };
            buffer_.reserve(2 * 1024 * 1024);
            writer->open(storage.reserve(), s, w, h, trackRoles, pixelFormat);
            storage.identifyActive();
        };
        openWriter();
        if (stop_)
            throw std::runtime_error("Recording cancelled during initialization");
        const double epoch = clockSeconds();
        segmentStart = epoch;
        buffer_.begin(epoch, s.retention, s.bufferBytes);
        buffer_.reserve(2 * 1024 * 1024);
        {
            std::lock_guard lock(queue.mutex);
            if (!queue.frames.empty()) {
                current = std::move(queue.frames.back());
                queue.frames.clear();
            }
            queue.captured = 1;
            queue.dropped = 0;
        }
        qint64 totalFrames = 0, totalSamples = 0;
        {
            std::lock_guard lock(mutex_);
            stats_.started = epoch;
            stats_.width = w;
            stats_.height = h;
            stats_.fps = s.fps;
            stats_.encoder = s.encoder;
            stats_.backend = queue.backend;
            initialized_ = true;
            ready_.notify_all();
        }
        while (true) {
            double target = epoch + double(totalFrames) / s.fps;
            if (stop_ && target >= stopAt_.load())
                break;
            if (clockSeconds() < target + 0.05) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            double lag = clockSeconds() - target - 0.05;
            if (lag > 2.)
                throw std::runtime_error("Encoder backlog exceeded two seconds. Recording stopped; lower "
                                         "resolution, frame rate, or encoder cost after reviewing settings.");
            {
                std::lock_guard lock(queue.mutex);
                if (!queue.error.isEmpty())
                    throw std::runtime_error(queue.error.toStdString());
                while (!queue.frames.empty() && queue.frames.front().timestamp <= target) {
                    current = std::move(queue.frames.front());
                    queue.frames.pop_front();
                }
            }
            if (!audio.error().isEmpty())
                throw std::runtime_error(audio.error().toStdString());
            if (!writer) {
                segmentStart = target;
                openWriter();
            }
            writer->addVideo(current);
            qint64 nextSamples = qRound64(double(totalFrames + 1) * 48000 / s.fps);
            writer->addAudio(
                audio.read(epoch + double(totalSamples) / 48000., int(nextSamples - totalSamples)));
            totalSamples = nextSamples;
            ++totalFrames;
            const auto &app = current.fullscreenApp;
            if (!app.executable.isEmpty()) {
                const double end = epoch + double(totalFrames) / s.fps;
                if (!fullscreen.isEmpty() && fullscreen.last().end == target &&
                    fullscreen.last().executable == app.executable)
                    fullscreen.last().end = end;
                else
                    fullscreen.append({target, end, app.name, app.executable});
            }
            {
                std::lock_guard lock(mutex_);
                stats_.captured = queue.captured;
                stats_.dropped = queue.dropped;
                stats_.encoded = quint64(totalFrames);
                stats_.backlog = qMax(0, int(lag * s.fps));
                stats_.audioDiscontinuities = audio.discontinuities();
                stats_.audioNotice = audio.notice();
            }
            if (writer->frames >= s.fps * 2)
                commit();
        }
        commit();
    } catch (const std::exception &e) {
        QString error = QString::fromUtf8(e.what());
        try {
            commit();
        } catch (const std::exception &save) {
            error += "; final segment: " + QString::fromUtf8(save.what());
        }
        std::lock_guard lock(mutex_);
        stats_.error = error;
        initialized_ = true;
        ready_.notify_all();
    } catch (const winrt::hresult_error &e) {
        std::lock_guard lock(mutex_);
        stats_.error = QString::fromWCharArray(e.message().c_str());
        initialized_ = true;
        ready_.notify_all();
    }
    queue.stop = true;
    if (capture.joinable())
        capture.join();
    audio.stop();
    writer.reset();
    try {
        buffer_.reserve(0);
    } catch (const std::exception &e) {
        qWarning("Replay reservation release: %s", e.what());
    }
    storage.stop();
    buffer_.stop();
    {
        std::lock_guard lock(mutex_);
        stats_.elapsed = stats_.started ? clockSeconds() - stats_.started : 0;
    }
    active_ = false;
    winrt::uninit_apartment();
}
static QList<int> clipAudioStreams(const QString &path, const Settings &settings) {
    if (!settings.clipMic && !settings.clipDesktop)
        return {};
    AVFormatContext *input = nullptr;
    check(avformat_open_input(&input, path.toUtf8().constData(), nullptr, nullptr), "Read replay audio tracks");
    const auto close = qScopeGuard([&] { avformat_close_input(&input); });
    QList<int> selected;
    for (unsigned i = 0; i < input->nb_streams; ++i) {
        const auto *stream = input->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        const auto *tag = av_dict_get(stream->metadata, "sipping_audio_role", nullptr, 0);
        const auto role = tag ? QByteArrayView(tag->value) : QByteArrayView();
        if (settings.clipMic && settings.clipDesktop) {
            if (!role.startsWith("stem-"))
                selected << int(i);
        } else if (role.isEmpty()) {
            throw std::runtime_error("This replay was buffered without separate audio. Restart recording to "
                                     "use Mic and Desktop Audio clip controls.");
        } else if ((settings.clipMic && role.endsWith("-mic")) ||
                   (settings.clipDesktop && role.endsWith("-desktop")))
            selected << int(i);
    }
    return selected;
}

QString saveInterval(const Interval &interval, const Settings &s, const QString &output,
                     std::atomic_bool *cancel) {
    if (interval.segments.isEmpty())
        throw std::runtime_error("No pinned source segments");
    QTemporaryDir temp(QDir::tempPath() + "/clip-save-XXXXXX");
    auto list = temp.filePath("sources.ffconcat");
    QFile file(list);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot create source list");
    QByteArray content = "ffconcat version 1.0\n";
    for (const auto &segment : interval.segments) {
        QString path = segment.path;
        path.replace("'", "'\\''");
        content += "file '" + path.toUtf8() + "'\n";
        content += "duration " + QByteArray::number(segment.end - segment.start, 'f', 9) + "\n";
    }
    file.write(content);
    file.close();
    auto info = inspect(interval.segments.first().path);
    const auto audioStreams = clipAudioStreams(interval.segments.first().path, s);
    QString encoder = s.encoder;
    if (encoder == "auto")
        throw std::runtime_error("Export requires a resolved encoder snapshot");
    QString partial = output + ".partial";
    QStringList args{
        "-hide_banner", "-v",
        "error",        "-y",
        "-f",           "concat",
        "-safe",        "0",
        "-i",           list,
        "-ss",          QString::number(interval.start - interval.segments.first().start, 'f', 9),
        "-t",           QString::number(interval.duration(), 'f', 9),
        "-map",         "0:v:0",
        "-c:v",         encoder};
    for (const int stream : audioStreams)
        args << "-map" << QString("0:%1").arg(stream);
    if (!audioStreams.isEmpty())
        args << "-c:a" << (s.container == "webm" ? "libopus" : s.container == "mkv" ? "flac" : "aac");
    // Exact interval export: source remains immutable; codec implementation was
    // captured with the job. Container validity is checked before submitting jobs.
    QStringList filters;
    if (s.height > 0 && s.height < info.height)
        filters << QString("scale=-2:%1").arg(s.height);
    if (s.fps > 0 && s.fps < info.fps - 0.1)
        filters << QString("fps=%1").arg(s.fps);
    if (!filters.isEmpty())
        args << "-vf" << filters.join(',');
    args << encoderArguments(encoder, s.quality) << colorArguments(info);
    args << "-f" << (s.container == "mkv" ? "matroska" : s.container) << partial;
    auto result = runProcess(ffmpegPath(), args, 0, cancel);
    if (result.code != 0) {
        QFile::remove(partial);
        throw std::runtime_error(result.cancelled ? "Cancelled"
                                                  : QString::fromUtf8(result.output).toStdString());
    }
    if (!decodeValidate(partial, cancel)) {
        QFile::remove(partial);
        throw std::runtime_error("Saved original failed complete decoding validation");
    }
    auto saved = inspect(partial);
    if (saved.audioTracks != audioStreams.size()) {
        QFile::remove(partial);
        throw std::runtime_error("Saved clip audio did not match the selected tracks");
    }
    if (info.hdrMetadata != saved.hdrMetadata) {
        QFile::remove(partial);
        throw std::runtime_error("Saved clip lost available HDR metadata");
    }
    if (info.hdr && (!saved.hdr || saved.primaries != info.primaries || saved.transfer != info.transfer ||
                     saved.matrix != info.matrix)) {
        QFile::remove(partial);
        throw std::runtime_error("Saved clip did not preserve HDR colour properties");
    }
    if (std::abs(saved.duration - interval.duration()) >
        1. / qMax(1., saved.fps) + saved.audioRounding + 0.001) {
        QFile::remove(partial);
        throw std::runtime_error("Saved clip duration failed endpoint validation");
    }
    if (!QFile::rename(partial, output))
        throw std::runtime_error("Cannot finalize saved original");
    return output;
}
} // namespace clip
