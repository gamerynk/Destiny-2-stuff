#include <Windows.h>

#include <dxgi.h>
#include <atomic>

#include "graphics_renderer_report.h"
#include "state.h"

namespace dawn::client::hooks::graphics::renderer {
namespace {

std::atomic_bool g_nativeRecoveryPending{};

[[nodiscard]] bool is_device_loss(HRESULT result) noexcept {
    return result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DEVICE_REMOVED
           || result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

} // namespace

// drop our RTV before ResizeBuffers: DXGI sends WM_SIZE and the app then replaces the back
// buffers, which is the lifetime boundary for our view (fullscreen too)
void before_surface_change(IDXGISwapChain* swapChain) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    if (g_resources.swapChain == swapChain) {
        if (g_resources.activeSurfaceChanges == 0) {
            // the first of any overlapping calls drops the RTV until all have returned
            release_render_target(g_resources);
        }
        ++g_resources.activeSurfaceChanges;
    }
    ReleaseSRWLockExclusive(&g_rendererLock);
}

void after_surface_change(IDXGISwapChain* swapChain, HRESULT result) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    if (g_resources.swapChain == swapChain && g_resources.activeSurfaceChanges != 0) {
        g_resources.surfaceChangeDeviceLost =
            g_resources.surfaceChangeDeviceLost || is_device_loss(result);
        --g_resources.activeSurfaceChanges;
        if (g_resources.activeSurfaceChanges == 0) {
            const bool targetReady = kTransientRenderTarget || create_render_target(g_resources);
            const bool deviceLost = g_resources.surfaceChangeDeviceLost;
            g_resources.surfaceChangeDeviceLost = false;
            if (deviceLost || !targetReady) {
                report::note(report::Stage::shutdown,
                             deviceLost ? report::Reason::deviceLost
                                        : report::Reason::rebuildTarget);
                (void)shutdown_locked();
            }
        }
    }
    ReleaseSRWLockExclusive(&g_rendererLock);
}

void render_present_frame_locked() noexcept {
    if constexpr (kTransientRenderTarget) {
        if (!create_render_target(g_resources)) {
            release_render_target(g_resources);
            // a temporary acquisition failure keeps input/layout ownership for retry; a lost device
            // can't recover and needs the same cleanup as a failed native Present
            if (g_resources.device && FAILED(g_resources.device->GetDeviceRemovedReason())) {
                report::note(report::Stage::shutdown, report::Reason::deviceLost);
                (void)shutdown_locked();
            }
            return;
        }
    }
    render_frame_locked();
    if constexpr (kTransientRenderTarget) {
        release_render_target(g_resources);
    }
}

void present_result(IDXGISwapChain* swapChain, HRESULT result) noexcept {
    if (!is_device_loss(result)) {
        return;
    }
    AcquireSRWLockExclusive(&g_rendererLock);
    if (g_resources.swapChain == swapChain) {
        report::note(report::Stage::shutdown, report::Reason::deviceLost);
        (void)shutdown_locked();
    }
    ReleaseSRWLockExclusive(&g_rendererLock);
}

void check_native_device_loss() noexcept {
    AcquireSRWLockShared(&g_rendererLock);
    const bool lost = g_resources.device && FAILED(g_resources.device->GetDeviceRemovedReason());
    ReleaseSRWLockShared(&g_rendererLock);
    if (lost) g_nativeRecoveryPending.store(true, std::memory_order_release);
}

bool service_pending_shutdown() noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    bool released = true;
    if (g_nativeRecoveryPending.load(std::memory_order_acquire)
        || (g_resources.swapChain && !fully_active_locked())) {
        if (g_nativeRecoveryPending.load(std::memory_order_relaxed))
            report::note(report::Stage::shutdown, report::Reason::deviceLost);
        released = shutdown_locked();
        if (released) g_nativeRecoveryPending.store(false, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_rendererLock);
    return released;
}

} // namespace dawn::client::hooks::graphics::renderer
