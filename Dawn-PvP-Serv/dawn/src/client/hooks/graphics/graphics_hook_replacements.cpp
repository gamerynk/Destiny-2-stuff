#include "graphics_hook_replacements.h"

#include <atomic>
#include <cstdint>

#include "graphics_hook_lifecycle.h"

namespace dawn::client::hooks::graphics::replacement {
namespace {

__declspec(noinline) bool enter_hook_call() noexcept {
    if (g_hookCallDepth == 0) {
        // publish the count before the lock wait so a queued writer cannot skip this call
        g_activeHookCalls.fetch_add(1, std::memory_order_acq_rel);
        AcquireSRWLockShared(&g_hookLock);
    }
    const bool rendererEnabled = g_acceptRendererCalls;
    ++g_hookCallDepth;
    return rendererEnabled;
}

__declspec(noinline) void leave_hook_call() noexcept {
    --g_hookCallDepth;
    if (g_hookCallDepth == 0) {
        ReleaseSRWLockShared(&g_hookLock);
        g_activeHookCalls.fetch_sub(1, std::memory_order_acq_rel);
    }
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE present_body(IDXGISwapChain* swapChain,
                                                            UINT syncInterval,
                                                            UINT flags) noexcept {
    HRESULT result = DXGI_ERROR_INVALID_CALL;
    const bool rendererEnabled = enter_hook_call();
    const auto call = original<Present>(HookSlot::present);
    if (call != nullptr) {
        if (rendererEnabled && (flags & DXGI_PRESENT_TEST) == 0) {
            renderer::present(swapChain);
        }
        result = call(swapChain, syncInterval, flags);
        if (rendererEnabled) {
            renderer::present_result(swapChain, result);
        }
    }
    leave_hook_call();
    // thread-affine capture release runs only after the hook lifetime lock is gone
    renderer::dispatch_pending_input_release();
    return result;
}

__declspec(noinline) HRESULT STDMETHODCALLTYPE resize_buffers_body(IDXGISwapChain* swapChain,
                                                                   UINT bufferCount,
                                                                   UINT width,
                                                                   UINT height,
                                                                   DXGI_FORMAT format,
                                                                   UINT flags) noexcept {
    const bool rendererEnabled = enter_hook_call();
    const auto call = original<ResizeBuffers>(HookSlot::resizeBuffers);
    HRESULT result = DXGI_ERROR_INVALID_CALL;
    if (call != nullptr) {
        if (rendererEnabled) {
            renderer::before_surface_change(swapChain);
        }
        result = call(swapChain, bufferCount, width, height, format, flags);
        if (rendererEnabled) {
            renderer::after_surface_change(swapChain, result);
        }
    }
    leave_hook_call();
    return result;
}

} // namespace

EntryPoints entry_points() noexcept {
    return {reinterpret_cast<void*>(&present_body),
            reinterpret_cast<void*>(&resize_buffers_body)};
}

ProtectedEntries protected_entries() noexcept {
    return {
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&present_body)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&resize_buffers_body)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&enter_hook_call)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&leave_hook_call)},
    };
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swapChain,
                                  UINT syncInterval,
                                  UINT flags) noexcept {
    return present_body(swapChain, syncInterval, flags);
}

} // namespace dawn::client::hooks::graphics::replacement
