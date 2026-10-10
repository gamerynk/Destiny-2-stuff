#include "native_window_presentation.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "../../../core/logging/log.h"
#include "graphics_hook_replacements.h"

#if defined(RESTORATION_NATIVE_WINDOW_PRESENTATION) && RESTORATION_NATIVE_WINDOW_PRESENTATION
#if !defined(RESTORATION_NATIVE_RESIZE_OMISSION) || !RESTORATION_NATIVE_RESIZE_OMISSION \
    || !defined(RESTORATION_TRANSIENT_RTV) || !RESTORATION_TRANSIENT_RTV
#error Native window presentation requires native resize ownership and transient render targets.
#endif
#include "native_window_signatures.h"
#endif

namespace dawn::client::hooks::graphics::native_window {
namespace {

using Acquire = std::uintptr_t(__fastcall*)(std::uintptr_t, int);
using Release = bool(__fastcall*)();
struct Metadata {
    std::uintptr_t base{};
    Acquire acquire{};
    Release release{};
};

// metadata is guarded by g_hookLock; admitted frames copy it and publish their count
// before waiting, so shutdown cannot clear it until they leave
Metadata g_metadata{};
HWND g_window{};
bool g_selected{};
std::atomic_bool g_fallback{};
std::atomic_bool g_firstDraw{};
thread_local unsigned g_frameDepth{};
thread_local unsigned g_prepareDepth{};

enum class FrameResult { skipped, pinned, drawn, unsupported, fault, releaseFailed };
struct PinnedFrame { IDXGISwapChain* chain{}; ID3D11DeviceContext* context{}; };

template <typename Value>
Value read(std::uintptr_t address) noexcept {
    return *reinterpret_cast<const volatile Value*>(address);
}

// uses the native module's static TLS index, not this DLL's TLS or a foreign callback
std::uintptr_t thread_lease(std::uintptr_t base) noexcept {
    const auto index = read<unsigned>(base + 0x330F0E0);
    if (index >= 1088) return ~std::uintptr_t{};
    const auto slots = __readgsqword(0x58);
    if (!slots) return ~std::uintptr_t{};
    const auto moduleTls = read<std::uintptr_t>(slots + index * sizeof(std::uintptr_t));
    return moduleTls ? read<std::uintptr_t>(moduleTls + 0x2558) : ~std::uintptr_t{};
}

bool runtime_supported(std::uintptr_t base) noexcept {
    // native default modes 1/3 wait for the window message's semaphore ack; an override can
    // drop that wait and let a later frame overwrite our drawing
    return read<unsigned char>(base + 0x1F89E04) == 1
        && read<std::int32_t>(base + 0x1FD0854) == -1
        && read<std::uintptr_t>(base + 0x3CC77B0)
            == reinterpret_cast<std::uintptr_t>(&GetCurrentThreadId);
}

void request_fallback() noexcept {
    g_fallback.store(true, std::memory_order_release);
}

FrameResult with_owner(const Metadata& metadata, HWND window,
                       PinnedFrame& pinned, bool submit) noexcept {
    std::uintptr_t lease{};
    FrameResult result = FrameResult::skipped;
    bool released = true;
    bool rendererEntered = false;
    bool sdkCallEntered = false;
    bool nativeCallEntered = false;
    __try {
        __try {
            if (!runtime_supported(metadata.base)) return FrameResult::unsupported;
            if (read<HWND>(metadata.base + 0x26869C8) != window
                || read<unsigned>(metadata.base + 0x2814274) != 1) return result;
            // native ownership is nonrecursive; do not wait on ownership we already hold
            if (thread_lease(metadata.base) != 0) return FrameResult::unsupported;
            const auto pool = read<std::uintptr_t>(metadata.base + 0x2817910);
            if (!pool) return result;
            // this can wait for the publisher, so no DLL lock is held across it
            nativeCallEntered = true;
            lease = metadata.acquire(pool, 0);
            nativeCallEntered = false;
            if (!lease || lease != read<std::uintptr_t>(metadata.base + 0x2817910) + 0x10
                || thread_lease(metadata.base) != lease
                || read<unsigned>(lease + 0x14) != 1
                || read<DWORD>(lease + 0x18) != GetCurrentThreadId()) {
                result = FrameResult::unsupported;
            } else if (!runtime_supported(metadata.base)) {
                result = FrameResult::unsupported;
            } else if (read<unsigned>(metadata.base + 0x2814274) == 1) {
                const auto parent = read<std::uintptr_t>(lease);
                const auto chain = read<IDXGISwapChain*>(metadata.base + 0x2817948);
                const auto context = read<ID3D11DeviceContext*>(lease + 0x68);
                if (!parent || !chain || !context
                    || read<unsigned>(parent + 0x1C60) != read<unsigned>(parent + 0x1C64)) {
                    result = FrameResult::unsupported;
                } else if (!submit) {
                    // only COM lifetime operations under this first lease; hold no backbuffer while
                    // CPU/Win32 prep can reenter native presentation or resize
                    sdkCallEntered = true;
                    chain->AddRef();
                    pinned.chain = chain;
                    context->AddRef();
                    pinned.context = context;
                    sdkCallEntered = false;
                    result = FrameResult::pinned;
                } else if (chain == pinned.chain && context == pinned.context) {
                    rendererEntered = true;
                    result = renderer::submit_native_frame(chain, context)
                        ? FrameResult::drawn : FrameResult::unsupported;
                }
            }
        } __finally {
            // between admission and release, submission only does SDK context/device work, so it
            // cannot call native owner setters, Win32, UI prep or a foreign ImGui draw callback
            if (lease) {
                nativeCallEntered = true;
                released = metadata.release();
                nativeCallEntered = false;
            }
        }
    } __except (rendererEntered || nativeCallEntered || sdkCallEntered
        ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {
        // a native metadata fault is retryable; a renderer fault is not swallowed, since that
        // could strand a native/renderer lock or ImGui state
        result = FrameResult::fault;
    }
    return released ? result : FrameResult::releaseFailed;
}

HWND native_window(const Metadata& metadata, bool& supported) noexcept {
    HWND window{};
    supported = true;
    __try {
        // missing pool/window is ordinary startup, not a native ownership failure
        if (!read<std::uintptr_t>(metadata.base + 0x2817910)) return nullptr;
        supported = runtime_supported(metadata.base);
        if (!supported) return nullptr;
        window = read<HWND>(metadata.base + 0x26869C8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { supported = false; return nullptr; }
    if (!window || !IsWindow(window) || !IsWindowVisible(window)) return nullptr;
    DWORD process{};
    if (!GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()
        || GetAncestor(window, GA_ROOT) != window
        || static_cast<std::uintptr_t>(GetClassLongPtrW(window, GCLP_WNDPROC))
            != metadata.base + 0x3C0400) {
        supported = false;
        return nullptr;
    }
    return window;
}

} // namespace

bool resolve() noexcept {
#if defined(RESTORATION_NATIVE_WINDOW_PRESENTATION) && RESTORATION_NATIVE_WINDOW_PRESENTATION
    if (g_selected || g_window) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!signatures::matches(base)) return false;
    g_metadata = {base, reinterpret_cast<Acquire>(base + 0x1116D30),
        reinterpret_cast<Release>(base + 0x111E4E0)};
    g_fallback.store(false, std::memory_order_release);
    g_firstDraw.store(false, std::memory_order_release);
    g_selected = true;
    return true;
#else
    return false;
#endif
}

bool selected() noexcept { return g_selected; }
bool in_preparation() noexcept { return g_prepareDepth != 0; }
bool fallback_requested() noexcept { return g_fallback.load(std::memory_order_acquire); }

bool service_bootstrap() noexcept {
    if (!g_selected || fallback_requested()) return false;
    bool supported{};
    const HWND window = native_window(g_metadata, supported);
    if (!supported || (g_window && window && g_window != window)) {
        request_fallback();
        return false;
    }
    if (!window) return true;
    if (input::install(window)) {
        if (!g_window) core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=graphics stage=native_window_bootstrap result=bound");
        g_window = window;
        return true;
    }
    request_fallback();
    return false;
}

bool shutdown() noexcept {
    if (!hook_calls_idle()) return false;
    if (g_window && !input::uninstall()) return false;
    g_window = nullptr;
    g_metadata = {};
    g_selected = false;
    g_fallback.store(false, std::memory_order_release);
    return true;
}

FrameScope::FrameScope(HWND window) noexcept {
    const DWORD error = GetLastError();
    g_activeHookCalls.fetch_add(1, std::memory_order_acq_rel);
    AcquireSRWLockShared(&g_hookLock);
    const Metadata metadata = g_metadata;
    admitted_ = g_selected && g_acceptRendererCalls && !fallback_requested()
        && g_window == window && GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId();
    ReleaseSRWLockShared(&g_hookLock);
    if (!admitted_) {
        g_activeHookCalls.fetch_sub(1, std::memory_order_acq_rel);
        SetLastError(error);
        return;
    }
    ++g_hookCallDepth;
    outermost_ = ++g_frameDepth == 1;
    if (outermost_) {
        PinnedFrame pinned;
        FrameResult result = with_owner(metadata, window, pinned, false);
        if (result == FrameResult::pinned) {
            // Win32/UI callbacks run with no graphics owner; same-thread window reentry reuses the
            // held renderer prep lock
            ++g_prepareDepth;
            const bool prepared = renderer::prepare_native_frame(pinned.chain, pinned.context);
            --g_prepareDepth;
            result = prepared ? with_owner(metadata, window, pinned, true) : FrameResult::unsupported;
        }
        if (pinned.context) pinned.context->Release();
        if (pinned.chain) pinned.chain->Release();
        if (result == FrameResult::unsupported || result == FrameResult::fault
            || result == FrameResult::releaseFailed) request_fallback();
        if (result == FrameResult::drawn && !g_firstDraw.exchange(true, std::memory_order_acq_rel))
            core::log::write(core::log::Channel::client, core::log::Level::info,
                "ev=graphics stage=native_window_frame result=drawn ownership=released_before_forward");
    }
    SetLastError(error);
}

FrameScope::~FrameScope() noexcept {
    if (!admitted_) return;
    const DWORD error = GetLastError();
    if (outermost_) {
        renderer::check_native_device_loss();
        // the native owner is released now, so cursor/input policy can dispatch focus messages
        renderer::finish_native_frame();
    }
    --g_frameDepth;
    --g_hookCallDepth;
    g_activeHookCalls.fetch_sub(1, std::memory_order_acq_rel);
    SetLastError(error);
}

} // namespace dawn::client::hooks::graphics::native_window
