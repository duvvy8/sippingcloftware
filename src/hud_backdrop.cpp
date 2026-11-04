#include "hud_backdrop.h"
#include "platform.h"
#include <QScopeGuard>
#include <DispatcherQueue.h>
#include <dwmapi.h>
#include <roapi.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <array>

namespace clip {
namespace {
using namespace winrt;
using namespace winrt::Windows::UI::Composition;

void ensureCompositionQueue() {
    struct Apartment {
        HRESULT status = RoInitialize(RO_INIT_SINGLETHREADED);
        ~Apartment() {
            if (SUCCEEDED(status))
                RoUninitialize();
        }
    };
    thread_local Apartment apartment;
    if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
        check_hresult(apartment.status);
    if (winrt::Windows::System::DispatcherQueue::GetForCurrentThread())
        return;
    thread_local com_ptr<ABI::Windows::System::IDispatcherQueueController> queue;
    const DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
    check_hresult(CreateDispatcherQueueController(options, queue.put()));
}

LRESULT CALLBACK backdropWindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(window, message, w, l);
}

HWND createBackdropWindow() {
    const auto instance = GetModuleHandleW(nullptr);
    static const ATOM type = [instance] {
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = backdropWindowProc;
        cls.lpszClassName = L"SippingCloftwareHudGlass";
        return RegisterClassW(&cls);
    }();
    if (!type)
        throw_last_error();
    const auto window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE |
                                            WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
                                        MAKEINTATOM(type), L"", WS_POPUP, 0, 0, 0, 0,
                                        nullptr, nullptr, instance, nullptr);
    if (!window)
        throw_last_error();
    return window;
}
} // namespace

struct HudBackdrop::Impl {
    HWND window = nullptr;
    Compositor compositor{nullptr};
    Desktop::DesktopWindowTarget target{nullptr};
    std::array<SpriteVisual, 2> sprites{nullptr, nullptr};
    std::array<CompositionRoundedRectangleGeometry, 2> shapes{nullptr, nullptr};
    bool tried = false;

    void reset() {
        target = nullptr;
        sprites = {nullptr, nullptr};
        shapes = {nullptr, nullptr};
        compositor = nullptr;
        if (window) {
            DestroyWindow(window);
            window = nullptr;
        }
    }

    ~Impl() { reset(); }

    void initialize() {
        tried = true;
        ensureCompositionQueue();
        window = createBackdropWindow();
        const BOOL enabled = TRUE;
        check_hresult(DwmSetWindowAttribute(window, DWMWA_USE_HOSTBACKDROPBRUSH, &enabled, sizeof(enabled)));
        if (!excludeFromCapture(quintptr(window)))
            throw_last_error();
        compositor = Compositor();
        const auto interop = compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        check_hresult(interop->CreateDesktopWindowTarget(window, FALSE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget **>(put_abi(target))));
        auto root = compositor.CreateContainerVisual();
        target.Root(root);
        auto brush = compositor.CreateHostBackdropBrush();
        for (size_t i = 0; i < sprites.size(); ++i) {
            shapes[i] = compositor.CreateRoundedRectangleGeometry();
            sprites[i] = compositor.CreateSpriteVisual();
            sprites[i].Brush(brush);
            sprites[i].Clip(compositor.CreateGeometricClip(shapes[i]));
            root.Children().InsertAtTop(sprites[i]);
        }
    }
};

HudBackdrop::HudBackdrop() : impl_(std::make_unique<Impl>()) {}
HudBackdrop::~HudBackdrop() = default;

bool HudBackdrop::update(HWND hud, const QList<QRect> &surfaces, qreal scale) {
    auto &state = *impl_;
    try {
        if (!state.tried)
            state.initialize();
        if (!state.window)
            return false;
        RECT client{};
        POINT origin{};
        if (!GetClientRect(hud, &client) || !ClientToScreen(hud, &origin)) {
            hide();
            return false;
        }
        auto region = CreateRectRgn(0, 0, 0, 0);
        if (!region)
            throw_last_error();
        const auto freeRegion = qScopeGuard([&] {
            if (region)
                DeleteObject(region);
        });
        const int radius = qRound(6 * scale);
        for (size_t i = 0; i < state.sprites.size(); ++i) {
            const auto rect = surfaces.value(qsizetype(i));
            state.sprites[i].IsVisible(!rect.isEmpty());
            if (rect.isEmpty())
                continue;
            const int x = qRound(rect.x() * scale), y = qRound(rect.y() * scale);
            const int w = qRound(rect.width() * scale), h = qRound(rect.height() * scale);
            const winrt::Windows::Foundation::Numerics::float2 size{float(w), float(h)};
            state.sprites[i].Offset({float(x), float(y), 0});
            state.sprites[i].Size(size);
            state.shapes[i].Size(size);
            state.shapes[i].CornerRadius({float(radius), float(radius)});
            const auto shape = CreateRoundRectRgn(x, y, x + w, y + h, radius * 2, radius * 2);
            if (!shape)
                check_hresult(E_FAIL);
            const auto freeShape = qScopeGuard([shape] { DeleteObject(shape); });
            if (CombineRgn(region, region, shape, RGN_OR) == ERROR)
                check_hresult(E_FAIL);
        }
        if (!SetWindowRgn(state.window, region, TRUE))
            check_hresult(E_FAIL);
        region = nullptr; // Windows now owns the region.
        if (!SetWindowPos(state.window, hud, origin.x, origin.y, client.right, client.bottom,
                          SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOOWNERZORDER))
            throw_last_error();
        return true;
    } catch (const hresult_error &error) {
        qWarning("HUD Acrylic unavailable: 0x%08lx", static_cast<unsigned long>(error.code().value));
        state.reset();
        return false;
    }
}

void HudBackdrop::hide() {
    if (impl_->window)
        ShowWindow(impl_->window, SW_HIDE);
}
} // namespace clip
