#include <Windows.h>

#include <array>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "../../../../core/ui/layout/layout.h"
#include "../../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../cursor/runtime.h"
#include "../../polled_input/runtime.h"
#include "../input/input.h"
#include "graphics_renderer_report.h"
#include "state.h"
#include "renderer.h"

namespace dawn::client::hooks::graphics::renderer {
namespace {

constexpr UINT kBackBufferIndex = 0;
bool g_nativePrepared{};
bool g_nativeHasDraw{};

// the capture release must run on the window's own thread
[[nodiscard]] bool wait_for_capture_release(Resources& resources) noexcept {
    if (resources.window == nullptr) {
        return false;
    }
    if (IsWindow(resources.window) == FALSE) {
        if (g_captureReleaseWindow == resources.window) {
            g_captureReleaseWindow = nullptr;
        }
        return false;
    }

    const DWORD windowThread = GetWindowThreadProcessId(resources.window, nullptr);
    if (windowThread == 0) {
        return false;
    }
    HWND captured = nullptr;
    if (windowThread == GetCurrentThreadId()) {
        captured = GetCapture();
    } else {
        GUITHREADINFO information{};
        information.cbSize = sizeof(information);
        if (GetGUIThreadInfo(windowThread, &information) == FALSE) {
            // unknown cross-thread state stays pending until a later dispatch retries
            g_captureReleaseWindow = resources.window;
            return true;
        }
        captured = information.hwndCapture;
    }
    if (captured != resources.window) {
        if (g_captureReleaseWindow == resources.window) {
            g_captureReleaseWindow = nullptr;
        }
        return false;
    }

    g_captureReleaseWindow = resources.window;
    return windowThread != GetCurrentThreadId();
}

template <typename Interface> void release_com(Interface*& object) noexcept {
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

struct ViewFormat {
    DXGI_FORMAT stored{};
    DXGI_FORMAT view{};
};

constexpr std::array<ViewFormat, 6> kTypelessViewFormats{
    ViewFormat{DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM},
    ViewFormat{DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM},
    ViewFormat{DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM},
    ViewFormat{DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UNORM},
    ViewFormat{DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT},
    ViewFormat{DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_FLOAT},
};

[[nodiscard]] bool describe_view(ID3D11Texture2D* backBuffer,
                                 D3D11_RENDER_TARGET_VIEW_DESC& output) noexcept {
    D3D11_TEXTURE2D_DESC texture{};
    backBuffer->GetDesc(&texture);
    for (const ViewFormat& candidate : kTypelessViewFormats) {
        if (candidate.stored != texture.Format) {
            continue;
        }
        output = {};
        output.Format = candidate.view;
        output.ViewDimension = texture.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS
                                                            : D3D11_RTV_DIMENSION_TEXTURE2D;
        return true;
    }
    return false;
}

[[nodiscard]] bool shutdown_resources(Resources& resources) noexcept {
    if (wait_for_capture_release(resources)) {
        return false;
    }
    if (resources.inputInstalled) {
        if (!input::uninstall_raw_input_window()) {
            return false;
        }
        if (!input::uninstall()) {
            return false;
        }
        resources.inputInstalled = false;
    }
    if (resources.dx11BackendInitialized) {
        ImGui_ImplDX11_Shutdown();
        resources.dx11BackendInitialized = false;
    }
    if (resources.win32BackendInitialized) {
        ImGui_ImplWin32_Shutdown();
        resources.win32BackendInitialized = false;
    }
    if (resources.layoutInitialized) {
        if (!core::ui::layout::shutdown()) {
            return false;
        }
        resources.layoutInitialized = false;
    }
    release_resources(resources);
    return true;
}

[[nodiscard]] bool discard_staged(Resources& staged) noexcept {
    if (!shutdown_resources(staged)) {
        // publish ownership only so a later shutdown can free it without a leak
        g_resources = staged;
    }
    return false;
}

[[nodiscard]] bool initialize_locked(IDXGISwapChain* swapChain,
                                      ID3D11DeviceContext* nativeContext) noexcept {
    Resources staged;
    if (!selection::acquire(swapChain, staged)) {
        return false;
    }
    if (nativeContext && staged.context != nativeContext) {
        release_resources(staged);
        report::note(report::Stage::acquire, report::Reason::context);
        return false;
    }
    if (!core::ui::layout::initialize()) {
        release_resources(staged);
        report::note(report::Stage::init, report::Reason::layout);
        return false;
    }
    staged.layoutInitialized = true;
    if (!ImGui_ImplWin32_Init(staged.window)) {
        report::note(report::Stage::init, report::Reason::win32Backend);
        return discard_staged(staged);
    }
    staged.win32BackendInitialized = true;
    if (!ImGui_ImplDX11_Init(staged.device, staged.context)) {
        report::note(report::Stage::init, report::Reason::dx11Backend);
        return discard_staged(staged);
    }
    staged.dx11BackendInitialized = true;
    if (!input::install(staged.window)) {
        report::note(report::Stage::init, report::Reason::windowInput);
        return discard_staged(staged);
    }
    staged.inputInstalled = true;
    g_resources = staged;
    report::note_active();
    return true;
}

} // namespace

SRWLOCK g_rendererLock{SRWLOCK_INIT};
Resources g_resources{};
HWND g_captureReleaseWindow{};

bool create_render_target(Resources& resources) noexcept {
    if (resources.swapChain == nullptr || resources.device == nullptr
        || resources.renderTarget != nullptr) {
        return false;
    }
    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT bufferResult = resources.swapChain->GetBuffer(
        kBackBufferIndex, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(bufferResult) || backBuffer == nullptr) {
        release_com(backBuffer);
        report::note(report::Stage::target, report::Reason::backBuffer);
        return false;
    }
    HRESULT targetResult =
        resources.device->CreateRenderTargetView(backBuffer, nullptr, &resources.renderTarget);
    if (FAILED(targetResult)) {
        // the inferred description covers a typed buffer only
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        if (describe_view(backBuffer, view)) {
            targetResult = resources.device->CreateRenderTargetView(
                backBuffer, &view, &resources.renderTarget);
        } else {
            report::note(report::Stage::target, report::Reason::viewFormat);
        }
    }
    release_com(backBuffer);
    if (FAILED(targetResult) || resources.renderTarget == nullptr) {
        report::note(report::Stage::target, report::Reason::view);
        return false;
    }
    return true;
}

void release_render_target(Resources& resources) noexcept {
    release_com(resources.renderTarget);
}

void release_resources(Resources& resources) noexcept {
    release_render_target(resources);
    release_com(resources.context);
    release_com(resources.device);
    release_com(resources.swapChain);
    resources = {};
}

bool shutdown_locked() noexcept {
    return shutdown_resources(g_resources);
}

bool fully_active_locked() noexcept {
    return g_resources.swapChain != nullptr && g_resources.device != nullptr
           && g_resources.context != nullptr
           && (kTransientRenderTarget || g_resources.renderTarget != nullptr)
           && g_resources.layoutInitialized && g_resources.win32BackendInitialized
           && g_resources.dx11BackendInitialized && g_resources.inputInstalled
           && g_resources.activeSurfaceChanges == 0 && input::active(g_resources.window);
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    const bool released = shutdown_locked();
    ReleaseSRWLockExclusive(&g_rendererLock);
    return released;
}

bool active() noexcept {
    AcquireSRWLockShared(&g_rendererLock);
    const bool initialized = fully_active_locked();
    ReleaseSRWLockShared(&g_rendererLock);
    return initialized;
}

bool synchronize_resources_locked(IDXGISwapChain* swapChain, ID3D11DeviceContext* nativeContext) noexcept {
    if (nativeContext && g_resources.context && g_resources.context != nativeContext) {
        report::note(report::Stage::shutdown, report::Reason::surfaceLost);
        (void)shutdown_locked();
        return false;
    }
    if (g_resources.swapChain != nullptr && g_resources.swapChain != swapChain
        && g_resources.activeSurfaceChanges == 0
        && selection::matches_output_window(swapChain, g_resources.window)) {
        if (!shutdown_locked()) {
            return false;
        }
    }
    if (g_resources.swapChain != nullptr && !fully_active_locked()) {
        if (g_resources.activeSurfaceChanges != 0) {
            // Present can run at the same time as the original call, which owns the back buffer
            return false;
        }
        report::note(report::Stage::shutdown, report::Reason::surfaceLost);
        if (!shutdown_locked()) {
            return false;
        }
    }
    if (g_resources.swapChain == nullptr) {
        (void)initialize_locked(swapChain, nativeContext);
    }
    return g_resources.swapChain == swapChain && fully_active_locked();
}

void present(IDXGISwapChain* swapChain, ID3D11DeviceContext* nativeContext) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    if (synchronize_resources_locked(swapChain, nativeContext)) render_present_frame_locked();
    ReleaseSRWLockExclusive(&g_rendererLock);
    finish_native_frame();
}

bool prepare_native_frame(IDXGISwapChain* chain, ID3D11DeviceContext* context) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    g_nativePrepared = false;
    g_nativeHasDraw = false;
    if (synchronize_resources_locked(chain, context)) {
        // DX11 NewFrame creates device-only state; font/texture uploads happen in RenderDrawData,
        // reserved for submit_native_frame under native ownership
        g_nativeHasDraw = prepare_frame_locked();
        g_nativePrepared = fully_active_locked()
            && (!g_nativeHasDraw || native_draw_callbacks_supported_locked());
    }
    const bool prepared = g_nativePrepared;
    ReleaseSRWLockExclusive(&g_rendererLock);
    return prepared;
}

bool submit_native_frame(IDXGISwapChain* chain, ID3D11DeviceContext* context) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    // deliberately skip fully_active_locked: it queries Win32. Preparation checked the binding
    // and the counted frame lifetime blocks worker cleanup until we return
    bool submitted = g_nativePrepared && g_resources.swapChain == chain
        && g_resources.context == context && g_resources.device
        && g_resources.layoutInitialized && g_resources.dx11BackendInitialized
        && g_resources.win32BackendInitialized && g_resources.inputInstalled
        && g_resources.activeSurfaceChanges == 0;
    if (submitted && g_nativeHasDraw) {
        submitted = create_render_target(g_resources);
        if (submitted) draw_prepared_data_locked();
        release_render_target(g_resources);
    }
    g_nativePrepared = false;
    ReleaseSRWLockExclusive(&g_rendererLock);
    return submitted;
}

void finish_native_frame() noexcept {
    // the cursor policy calls Win32, so run it only after the renderer lock is gone
    const bool visible = core::ui::runtime::snapshot().visible;
    cursor::apply_visibility(visible);
    polled_input::apply_visibility(visible);
    (void)input::install_raw_input_window();
}

} // namespace dawn::client::hooks::graphics::renderer
