#include "capture.h"
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;
using ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
namespace clip {
struct Capture::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<IDXGIOutputDuplication> duplication;
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    IDirect3DDevice rtDevice{nullptr};
    bool floating = false, gotFrame = false;
    std::atomic_bool closed = false;
    QString backend;
    int width = 0, height = 0;
    double lastRead = 0, interval = 1. / 60;
    void read(ID3D11Texture2D *texture, CapturedFrame &out, int w, int h, double timestamp) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (int(desc.Width) < w || int(desc.Height) < h)
            throw std::runtime_error("Capture surface dimensions changed");
        if (!staging) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            desc.ArraySize = 1;
            desc.MipLevels = 1;
            check_hresult(device->CreateTexture2D(&desc, nullptr, &staging));
        }
        context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        check_hresult(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map));
        out.width = w;
        out.height = h;
        out.floating = floating;
        out.stride = w * (floating ? 8 : 4);
        out.timestamp = timestamp;
        out.pixels.resize(qsizetype(out.stride) * h);
        for (int y = 0; y < h; ++y)
            memcpy(out.pixels.data() + qsizetype(y) * out.stride,
                   static_cast<char *>(map.pData) + qsizetype(y) * map.RowPitch, out.stride);
        context->Unmap(staging.Get(), 0);
    }
};
Capture::Capture() : impl_(std::make_unique<Impl>()) {}
Capture::~Capture() {
    close();
}
void Capture::close() {
    if (!impl_)
        return;
    try {
        if (impl_->session)
            impl_->session.Close();
        if (impl_->pool)
            impl_->pool.Close();
    } catch (...) {
    }
    impl_ = std::make_unique<Impl>();
}
QString Capture::backend() const {
    return impl_->backend;
}
void Capture::open(const Settings &s, const Registry &r) {
    close();
    if (s.source == "window" && s.backend == "dxgi")
        throw std::runtime_error(
            "DXGI captures displays only. Select Windows Graphics Capture for a window source.");
    if (r.displays.isEmpty())
        throw std::runtime_error("No attached capture display");
    int index = qBound(0, s.monitor, int(r.displays.size()) - 1);
    auto display = r.displays[index];
    if (s.source == "window" && s.window) {
        HMONITOR monitor = MonitorFromWindow(HWND(s.window), MONITOR_DEFAULTTONEAREST);
        for (auto d : r.displays)
            if (d.handle == quintptr(monitor))
                display = d;
    }
    auto &p = *impl_;
    p.floating = display.hdr || s.hdr;
    p.interval = 1. / qMax(1, s.fps);
    if (s.hdr && !display.hdr)
        throw std::runtime_error("Selected display does not currently report HDR output");
    if (display.hdr && !s.hdr && !s.toneMapCapture)
        throw std::runtime_error(
            "This display is HDR. Enable Preserve HDR or explicitly approve tone-mapped SDR recording.");
    ComPtr<IDXGIFactory1> factory;
    check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;
    check_hresult(factory->EnumAdapters1(UINT(display.adapter), &adapter));
    D3D_FEATURE_LEVEL level;
    check_hresult(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                    &p.device, &level, &p.context));
    if (s.backend == "wgc")
        try {
            if (!GraphicsCaptureSession::IsSupported())
                throw std::runtime_error("Windows Graphics Capture is unsupported");
            auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (s.source == "window") {
                if (!s.window || !IsWindow(HWND(s.window)))
                    throw std::runtime_error("Selected window no longer exists");
                check_hresult(interop->CreateForWindow(HWND(s.window), guid_of<GraphicsCaptureItem>(),
                                                       put_abi(p.item)));
            } else
                check_hresult(interop->CreateForMonitor(HMONITOR(display.handle),
                                                        guid_of<GraphicsCaptureItem>(), put_abi(p.item)));
            auto size = p.item.Size();
            p.width = size.Width;
            p.height = size.Height;
            ComPtr<IDXGIDevice> dxgi;
            check_hresult(p.device.As(&dxgi));
            com_ptr<IInspectable> inspectable;
            check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
            p.rtDevice = inspectable.as<IDirect3DDevice>();
            p.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                p.rtDevice,
                p.floating ? DirectXPixelFormat::R16G16B16A16Float
                           : DirectXPixelFormat::B8G8R8A8UIntNormalized,
                3, size);
            p.session = p.pool.CreateCaptureSession(p.item);
            p.item.Closed([ptr = &p](auto &&, auto &&) { ptr->closed = true; });
            p.session.StartCapture();
            p.backend = "Windows Graphics Capture";
            return;
        } catch (...) {
            if (s.source == "window" || p.floating)
                throw; /* Explicitly reported monitor fallback; settings unchanged. */
        }
    if (p.floating)
        throw std::runtime_error("An HDR display requires floating-point Windows Graphics Capture; DXGI "
                                 "fallback cannot preserve or tone-map it");
    ComPtr<IDXGIOutput> o;
    check_hresult(adapter->EnumOutputs(UINT(display.output), &o));
    ComPtr<IDXGIOutput1> o1;
    check_hresult(o.As(&o1));
    check_hresult(o1->DuplicateOutput(p.device.Get(), &p.duplication));
    p.width = display.width;
    p.height = display.height;
    p.backend = "DXGI Desktop Duplication (SDR monitor fallback)";
}
bool Capture::next(CapturedFrame &out) {
    auto &p = *impl_;
    if (p.closed)
        throw std::runtime_error("Capture source closed or disconnected");
    if (p.pool) {
        auto frame = p.pool.TryGetNextFrame();
        if (!frame)
            return false;
        auto size = frame.ContentSize();
        if (size.Width != p.width || size.Height != p.height) {
            if (!p.gotFrame && size.Width > 0 && size.Height > 0) {
                p.width = size.Width;
                p.height = size.Height;
                p.staging.Reset();
                frame.Close();
                p.pool.Recreate(p.rtDevice,
                                p.floating ? DirectXPixelFormat::R16G16B16A16Float
                                           : DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                3, size);
                return false;
            }
            throw std::runtime_error(
                QString("Capture dimensions changed from %1x%2 to %3x%4. Recording stopped to preserve "
                        "settings; select the new source and restart.")
                    .arg(p.width)
                    .arg(p.height)
                    .arg(size.Width)
                    .arg(size.Height)
                    .toStdString());
        }
        double t = double(frame.SystemRelativeTime().count()) / 10000000.;
        if (t + 0.001 < p.lastRead + p.interval)
            return false;
        p.lastRead = t;
        auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));
        p.read(texture.Get(), out, p.width, p.height, t);
        p.gotFrame = true;
        return true;
    }
    if (p.duplication) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        auto hr = p.duplication->AcquireNextFrame(5, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return false;
        check_hresult(hr);
        try {
            ComPtr<ID3D11Texture2D> texture;
            check_hresult(resource.As(&texture));
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            double t = info.LastPresentTime.QuadPart
                           ? double(info.LastPresentTime.QuadPart) / double(freq.QuadPart)
                           : clockSeconds();
            if (t + 0.001 < p.lastRead + p.interval) {
                p.duplication->ReleaseFrame();
                return false;
            }
            p.lastRead = t;
            p.read(texture.Get(), out, p.width, p.height, t);
            p.duplication->ReleaseFrame();
            return true;
        } catch (...) {
            p.duplication->ReleaseFrame();
            throw;
        }
    }
    return false;
}
} // namespace clip
