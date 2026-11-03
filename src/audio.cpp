#include "audio.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;
namespace clip {
namespace {
void hrcheck(HRESULT hr, const char *message) {
    if (FAILED(hr))
        throw std::runtime_error(
            (QString(message) + QString(" (0x%1)").arg(quint32(hr), 8, 16, QChar('0'))).toStdString());
}
struct Activation
    : Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                   Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    ~Activation() {
        CloseHandle(done);
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override {
        ComPtr<IUnknown> object;
        HRESULT h = op->GetActivateResult(&result, &object);
        if (FAILED(h))
            result = h;
        if (SUCCEEDED(result))
            result = object.As(&client);
        SetEvent(done);
        return S_OK;
    }
};
struct Source {
    AudioSource config;
    std::thread worker;
    std::atomic_bool stop = false;
    mutable std::mutex mutex;
    std::deque<AudioBlock> blocks;
    QString error;
    std::atomic<quint64> discontinuities = 0;
    std::atomic_bool ready = false;
    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            ComPtr<IAudioClient> client;
            WAVEFORMATEX *allocated = nullptr;
            WAVEFORMATEX processFormat{};
            WAVEFORMATEX *format = nullptr;
            bool process = config.kind == "application";
            if (process) {
                if (!AudioEngine::processCaptureSupported())
                    throw std::runtime_error(
                        "Per-application audio requires Windows build 20348 or later. Select labelled "
                        "system/device capture on this Windows version.");
                auto completion = Microsoft::WRL::Make<Activation>();
                AUDIOCLIENT_ACTIVATION_PARAMS params{};
                params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
                params.ProcessLoopbackParams.TargetProcessId = config.pid;
                params.ProcessLoopbackParams.ProcessLoopbackMode =
                    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
                PROPVARIANT pv{};
                pv.vt = VT_BLOB;
                pv.blob.cbSize = sizeof(params);
                pv.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
                ComPtr<IActivateAudioInterfaceAsyncOperation> op;
                hrcheck(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                                    __uuidof(IAudioClient), &pv, completion.Get(), &op),
                        "Activate application audio");
                if (WaitForSingleObject(completion->done, 10000) != WAIT_OBJECT_0)
                    throw std::runtime_error("Application audio activation timed out");
                hrcheck(completion->result, "Application audio activation");
                client = completion->client;
                processFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                processFormat.nChannels = 2;
                processFormat.nSamplesPerSec = 48000;
                processFormat.wBitsPerSample = 32;
                processFormat.nBlockAlign = 8;
                processFormat.nAvgBytesPerSec = 384000;
                format = &processFormat;
            } else {
                ComPtr<IMMDeviceEnumerator> enumerator;
                hrcheck(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                         IID_PPV_ARGS(&enumerator)),
                        "Enumerate audio");
                ComPtr<IMMDevice> device;
                hrcheck(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(config.id.utf16()), &device),
                        "Selected audio device is unavailable; no replacement was selected");
                hrcheck(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client),
                        "Activate selected audio device");
                hrcheck(client->GetMixFormat(&allocated), "Read audio format");
                format = allocated;
            }
            std::unique_ptr<WAVEFORMATEX, void (*)(WAVEFORMATEX *)> owned(allocated,
                                                                          [](auto *p) { CoTaskMemFree(p); });
            AVSampleFormat sample = AV_SAMPLE_FMT_NONE;
            bool floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                floating = IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format)->SubFormat,
                                       KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            if (floating && format->wBitsPerSample == 32)
                sample = AV_SAMPLE_FMT_FLT;
            else if (format->wBitsPerSample == 16)
                sample = AV_SAMPLE_FMT_S16;
            else if (format->wBitsPerSample == 32)
                sample = AV_SAMPLE_FMT_S32;
            else
                throw std::runtime_error("Selected audio device uses an unsupported sample representation");
            AVChannelLayout input{}, output = AV_CHANNEL_LAYOUT_STEREO;
            av_channel_layout_default(&input, format->nChannels);
            SwrContext *swr = nullptr;
            check(swr_alloc_set_opts2(&swr, &output, AV_SAMPLE_FMT_FLT, 48000, &input, sample,
                                      int(format->nSamplesPerSec), 0, nullptr),
                  "Create audio resampler");
            av_channel_layout_uninit(&input);
            std::unique_ptr<SwrContext, void (*)(SwrContext *)> resampler(swr, [](auto *p) { swr_free(&p); });
            check(swr_init(swr), "Initialize audio resampler");
            DWORD flags = config.kind == "microphone" ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK;
            flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
            hrcheck(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, format, nullptr),
                    "Initialize audio stream");
            ComPtr<IAudioCaptureClient> capture;
            hrcheck(client->GetService(IID_PPV_ARGS(&capture)), "Open audio capture");
            hrcheck(client->Start(), "Start audio capture");
            ready = true;
            double expected = 0;
            int sinceDrift = 0;
            while (!stop) {
                UINT32 frames = 0;
                hrcheck(capture->GetNextPacketSize(&frames), "Audio device disconnected");
                if (!frames) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    continue;
                }
                BYTE *bytes = nullptr;
                DWORD packetFlags = 0;
                UINT64 position = 0, qpc = 0;
                hrcheck(capture->GetBuffer(&bytes, &frames, &packetFlags, &position, &qpc),
                        "Read audio packet");
                Q_UNUSED(position);
                if (packetFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                    ++discontinuities;
                double stamp = (packetFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                                   ? clockSeconds() - double(frames) / format->nSamplesPerSec
                                   : double(qpc) / 10000000.;
                int count = swr_get_out_samples(swr, int(frames));
                AudioBlock block;
                block.time = stamp;
                block.samples.resize(size_t(count) * 2);
                QByteArray silence;
                if (packetFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silence.resize(int(frames * format->nBlockAlign));
                    silence.fill(0);
                    bytes = reinterpret_cast<BYTE *>(silence.data());
                }
                const uint8_t *src[] = {bytes};
                uint8_t *dst[] = {reinterpret_cast<uint8_t *>(block.samples.data())};
                int converted = swr_convert(swr, dst, count, src, int(frames));
                capture->ReleaseBuffer(frames);
                check(converted, "Resample captured audio");
                block.samples.resize(size_t(converted) * 2);
                // QPC anchors every block to video. Correct the resampling rate gradually rather
                // than accumulating a separate device clock for a long recording.
                sinceDrift += converted;
                if (expected > 0 && sinceDrift >= 48000) {
                    double drift = stamp - expected;
                    if (std::abs(drift) < 0.02)
                        swr_set_compensation(swr, qBound(-24, int(drift * 48000), 24), 48000);
                    sinceDrift = 0;
                }
                expected = stamp + double(converted) / 48000.;
                bool held = (GetAsyncKeyState(config.pushKey) & 0x8000) != 0;
                if (config.pushModifiers & MOD_CONTROL)
                    held &= (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                if (config.pushModifiers & MOD_SHIFT)
                    held &= (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                if (config.pushModifiers & MOD_ALT)
                    held &= (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                if (config.pushModifiers & MOD_WIN)
                    held &= ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
                if (config.pushToTalk && !held)
                    std::fill(block.samples.begin(), block.samples.end(), 0.f);
                {
                    std::lock_guard lock(mutex);
                    blocks.push_back(std::move(block));
                    while (!blocks.empty() && stamp - blocks.front().time > 4.)
                        blocks.pop_front();
                }
            }
            client->Stop();
        } catch (const std::exception &e) {
            std::lock_guard lock(mutex);
            error = QString::fromUtf8(e.what());
            ready = true;
        }
        CoUninitialize();
    }
};
} // namespace
struct AudioEngine::Impl {
    std::vector<std::unique_ptr<Source>> sources;
};
AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() {
    stop();
}
QList<AudioDevice> AudioEngine::devices() {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment {
        bool owned;
        ~Apartment() {
            if (owned)
                CoUninitialize();
        }
    } apartment{SUCCEEDED(initialized)};
    QList<AudioDevice> list;
    ComPtr<IMMDeviceEnumerator> en;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
        for (auto flow : {eRender, eCapture}) {
            ComPtr<IMMDeviceCollection> collection;
            if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)))
                continue;
            ComPtr<IMMDevice> defaultDevice;
            LPWSTR defaultId = nullptr;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(flow, eConsole, &defaultDevice)))
                defaultDevice->GetId(&defaultId);
            QString defaultName = defaultId ? QString::fromWCharArray(defaultId) : QString();
            CoTaskMemFree(defaultId);
            UINT count = 0;
            collection->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> d;
                collection->Item(i, &d);
                LPWSTR id = nullptr;
                d->GetId(&id);
                AudioDevice item;
                item.id = QString::fromWCharArray(id);
                CoTaskMemFree(id);
                item.input = flow == eCapture;
                item.defaultDevice = item.id == defaultName;
                ComPtr<IPropertyStore> store;
                if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &store))) {
                    PROPVARIANT v{};
                    PropVariantInit(&v);
                    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                        item.name = QString::fromWCharArray(v.pwszVal);
                    PropVariantClear(&v);
                }
                list << item;
            }
        }
    return list;
}
bool AudioEngine::processCaptureSupported() {
    using Version = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    auto fn = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    return fn && fn(&v) == 0 && v.dwBuildNumber >= 20348;
}
void AudioEngine::start(const QList<AudioSource> &configs) {
    stop();
    bool system = false, application = false;
    QSet<QString> ids;
    for (auto c : configs)
        if (c.enabled) {
            if (ids.contains(c.id + QString::number(c.pid)))
                throw std::runtime_error("The same audio source was selected twice");
            ids.insert(c.id + QString::number(c.pid));
            system |= c.kind == "system";
            application |= c.kind == "application";
        }
    if (system && application)
        throw std::runtime_error("System output and per-application audio overlap. Disable one route to "
                                 "avoid recording the same sound twice.");
    for (auto c : configs)
        if (c.enabled) {
            auto s = std::make_unique<Source>();
            s->config = c;
            s->worker = std::thread([ptr = s.get()] { ptr->run(); });
            impl_->sources.push_back(std::move(s));
        }
    QElapsedTimer t;
    t.start();
    for (auto &s : impl_->sources)
        while (!s->ready && t.elapsed() < 12000)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!error().isEmpty()) {
        auto e = error();
        stop();
        throw std::runtime_error(e.toStdString());
    }
}
void AudioEngine::stop() {
    for (auto &s : impl_->sources)
        s->stop = true;
    for (auto &s : impl_->sources)
        if (s->worker.joinable())
            s->worker.join();
    impl_->sources.clear();
}
int AudioEngine::tracks() const {
    if (impl_->sources.empty())
        return 0;
    int n = 1;
    for (auto &s : impl_->sources)
        if (s->config.separate)
            ++n;
    return n;
}
QString AudioEngine::error() const {
    for (auto &s : impl_->sources) {
        std::lock_guard lock(s->mutex);
        if (!s->error.isEmpty())
            return s->config.name + ": " + s->error;
    }
    return {};
}
quint64 AudioEngine::discontinuities() const {
    quint64 n = 0;
    for (auto &s : impl_->sources)
        n += s->discontinuities;
    return n;
}
QList<std::vector<float>> AudioEngine::read(double start, int frames) {
    QList<std::vector<float>> out;
    if (impl_->sources.empty())
        return out;
    out.append(std::vector<float>(size_t(frames) * 2, 0.f));
    for (auto &s : impl_->sources) {
        std::vector<float> individual(size_t(frames) * 2, 0.f);
        {
            std::lock_guard lock(s->mutex);
            while (!s->blocks.empty() &&
                   s->blocks.front().time + double(s->blocks.front().samples.size() / 2) / 48000. <
                       start - 0.1)
                s->blocks.pop_front();
            for (const auto &b : s->blocks) {
                qint64 offset = qRound64((b.time - start) * 48000);
                int sourceStart = int(qMax(qint64(0), -offset)), destination = int(qMax(qint64(0), offset));
                int count = qMin(int(b.samples.size() / 2) - sourceStart, frames - destination);
                if (count <= 0)
                    continue;
                for (int i = 0; i < count * 2; ++i)
                    individual[size_t(destination) * 2 + i] =
                        b.samples[size_t(sourceStart) * 2 + i] * float(s->config.mute ? 0. : s->config.gain);
            }
        }
        if (s->config.mix)
            for (size_t i = 0; i < individual.size(); ++i)
                out[0][i] += individual[i];
        if (s->config.separate)
            out.append(std::move(individual));
    }
    for (auto &track : out)
        for (auto &sample : track)
            sample = qBound(-1.f, sample, 1.f);
    return out;
}
} // namespace clip
