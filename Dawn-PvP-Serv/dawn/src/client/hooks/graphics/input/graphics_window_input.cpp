#include <Windows.h>

#include <atomic>
#include <bit>

#include "../../../../core/ui/modules/logs/logs.h"
#include "../../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../renderer/renderer.h"
#include "../native_window_presentation.h"
#include "../../polled_input/runtime.h"
#include "../../cursor/native_cursor_tail_runtime.h"
#include "input.h"

namespace dawn::client::hooks::graphics::input {
namespace {

constexpr LRESULT kCapturedMessageResult = 1;

struct Binding {
    HWND window{};
    WNDPROC original{};
    bool installed{};
    DWORD thread{};
    std::uint64_t generation{};
};

Binding g_binding{};
std::atomic_uint g_activeCallbacks{};
SRWLOCK g_inputLock{SRWLOCK_INIT};
std::uint64_t g_bindingGeneration{};
std::atomic_uint64_t g_ownerRevision{};

class CallbackGuard final {
public:
    CallbackGuard() noexcept {
        g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    }

    ~CallbackGuard() noexcept {
        g_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    CallbackGuard(const CallbackGuard&) = delete;
    CallbackGuard& operator=(const CallbackGuard&) = delete;
};

[[nodiscard]] WNDPROC original_for(HWND window) noexcept {
    AcquireSRWLockShared(&g_inputLock);
    const WNDPROC original = g_binding.window == window ? g_binding.original : nullptr;
    ReleaseSRWLockShared(&g_inputLock);
    return original;
}

[[nodiscard]] bool is_key_message(UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN
           || message == WM_SYSKEYUP;
}

// release the input lock first; this runs game code
[[nodiscard]] LRESULT
forward(WNDPROC original, HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    if (original == nullptr) {
        return DefWindowProcW(window, message, word, value);
    }
    return polled_input::forward_with_native_focus(original, window, message, word, value);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    const CallbackGuard callbackGuard;
    // bump before any native forwarding; a nested entry invalidates old geometry
    if (g_ownerRevision.load(std::memory_order_relaxed) != UINT64_MAX)
        (void)g_ownerRevision.fetch_add(1, std::memory_order_acq_rel);
#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
    const cursor::tail::OwnerScope cursorTail{window, message};
#endif
    const WNDPROC original = original_for(window);
    const bool nativePreparation = native_window::in_preparation();
    if (message == kRequiredForwardMessage) {
        if (nativePreparation) {
            return forward(original, window, message, word, value);
        }
        const native_window::FrameScope nativeFrame{window};
        const LRESULT result = forward(original, window, message, word, value);
        renderer::dispatch_pending_input_release(window);
        core::ui::modules::logs::dispatch_pending_copy(window);
        return result;
    }
    if (message == WM_CAPTURECHANGED) {
        // ReleaseCapture sends this synchronously while Dear ImGui holds the renderer lock
        return forward(original, window, message, word, value);
    }
    // the toggle key is ours in both states so the game never sees half a press
    const bool toggleKey =
        is_key_message(message)
        && static_cast<UINT>(word) == core::ui::runtime::snapshot().toggleVirtualKey;
    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
        if (!toggleKey || !cursor::tail::request_toggle(window, static_cast<UINT>(word)))
#endif
            (void)core::ui::runtime::toggle_for_key(static_cast<UINT>(word));
    }
    const bool captured = nativePreparation
        ? renderer::handle_preparation_message(window, message, word, value)
        : renderer::handle_window_message(window, message, word, value);
    if (captured || toggleKey) {
        if (!nativePreparation) renderer::dispatch_pending_input_release(window);
        if (message == WM_INPUT) {
            // the default procedure must run or the system keeps the raw-input buffer alive
            return DefWindowProcW(window, message, word, value);
        }
        return kCapturedMessageResult;
    }
    const LRESULT result = forward(original, window, message, word, value);
    if (!nativePreparation) renderer::dispatch_pending_input_release(window);
    return result;
}

} // namespace

bool install(HWND window) noexcept {
    if (window == nullptr || IsWindow(window) == FALSE) {
        return false;
    }
    AcquireSRWLockExclusive(&g_inputLock);
    if (g_binding.installed) {
        const bool sameWindow = g_binding.window == window;
        ReleaseSRWLockExclusive(&g_inputLock);
        return sameWindow;
    }
    if (g_activeCallbacks.load(std::memory_order_acquire) != 0) {
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR original =
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_procedure));
    if (original == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }
    if (g_bindingGeneration == UINT64_MAX) {
        (void)SetWindowLongPtrW(window, GWLP_WNDPROC, original);
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }
    g_binding = Binding{window, std::bit_cast<WNDPROC>(original), true,
        GetWindowThreadProcessId(window, nullptr), ++g_bindingGeneration};
    ReleaseSRWLockExclusive(&g_inputLock);
#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
    cursor::tail::binding_started();
#endif
    return true;
}

bool uninstall() noexcept {
#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
    if (!cursor::tail::prepare_retirement()) return false;
#endif
    AcquireSRWLockExclusive(&g_inputLock);
    if (!g_binding.installed) {
        const bool idle = g_activeCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_inputLock);
        return idle;
    }
    if (IsWindow(g_binding.window) == FALSE) {
        g_binding.installed = false;
        const bool idle = g_activeCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_inputLock);
        return idle;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR current = GetWindowLongPtrW(g_binding.window, GWLP_WNDPROC);
    const LONG_PTR replacement = reinterpret_cast<LONG_PTR>(&window_procedure);
    if (current == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }
    if (current == reinterpret_cast<LONG_PTR>(g_binding.original)) {
        g_binding.installed = false;
        const bool idle = g_activeCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_inputLock);
        return idle;
    }
    if (current != replacement) {
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR replaced = SetWindowLongPtrW(
        g_binding.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_binding.original));
    if (replaced == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_inputLock);
        return false;
    }
    g_binding.installed = false;
    const bool idle = g_activeCallbacks.load(std::memory_order_acquire) == 0;
    ReleaseSRWLockExclusive(&g_inputLock);
    return idle;
}

bool active(HWND window) noexcept {
    AcquireSRWLockShared(&g_inputLock);
    bool installed = g_binding.installed && g_binding.window == window && IsWindow(window) != FALSE
                     && IsWindowVisible(window) != FALSE;
    if (installed) {
        const LONG_PTR current = GetWindowLongPtrW(window, GWLP_WNDPROC);
        if (current == 0) {
            installed = false;
        } else {
            // a different non-original head is a later subclass that may still forward here
            installed = current != reinterpret_cast<LONG_PTR>(g_binding.original);
        }
    }
    ReleaseSRWLockShared(&g_inputLock);
    return installed;
}

MainInputSnapshot snapshot_main_input() noexcept {
    const DWORD error = GetLastError();
    AcquireSRWLockShared(&g_inputLock);
    const MainInputSnapshot result{g_binding.window, g_binding.thread, g_binding.generation,
        g_ownerRevision.load(std::memory_order_acquire), g_binding.installed};
    ReleaseSRWLockShared(&g_inputLock);
    SetLastError(error);
    return result;
}

} // namespace dawn::client::hooks::graphics::input
