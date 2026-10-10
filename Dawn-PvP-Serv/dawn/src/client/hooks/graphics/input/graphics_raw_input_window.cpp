// subclass of the game's hidden raw-mouse window (Tiger Input Window). Raw mouse goes
// there as WM_INPUT, never to the render window; a visible UI takes it, the rest is forwarded

#include <Windows.h>

#include <atomic>
#include <bit>

#include "../../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../polled_input/runtime.h"
#include "input.h"
#include "../../cursor/native_cursor_tail_runtime.h"

namespace dawn::client::hooks::graphics::input {
namespace {

constexpr wchar_t kRawInputWindowClass[] = L"Tiger Input Window";

struct RawBinding {
    HWND window{};
    WNDPROC original{};
    bool installed{};
    std::uint64_t generation{};
};

RawBinding g_rawBinding{};
RawCaptureClaim g_rawCaptureClaim{};
RawOpeningLease g_rawOpeningLease{};
std::uint64_t g_rawBindingGeneration{};
std::uint64_t g_rawClaimIdentity{};
std::atomic_uint g_rawActiveCallbacks{};
SRWLOCK g_rawLock{SRWLOCK_INIT};

struct LastError {
    DWORD value{GetLastError()};
    ~LastError() { SetLastError(value); }
};

std::uint64_t next_identity_locked(std::uint64_t& sequence) noexcept {
    if (sequence == UINT64_MAX) return 0;
    return ++sequence;
}

void retire_raw_binding_locked() noexcept {
    g_rawBinding.installed = false;
    g_rawBinding.generation = next_identity_locked(g_rawBindingGeneration);
    g_rawCaptureClaim = {};
    g_rawOpeningLease = {};
    // never reset the aggregate count: a retired procedure can still be returning
}

bool claim_matches(const RawCaptureClaim& claim, const RawBinding& binding,
    const core::ui::runtime::VisibilitySnapshot& visibility) noexcept {
    return claim.identity != 0 && claim.visibilityGeneration != 0
        && binding.installed && binding.generation != 0 && visibility.visible
        && visibility.generation == claim.visibilityGeneration
        && binding.generation == claim.bindingGeneration && binding.window == claim.window;
}

bool opening_matches(const RawOpeningLease& lease, const RawBinding& binding) noexcept {
    return lease.identity != 0 && lease.hiddenGeneration != 0 && binding.installed
        && binding.generation != 0 && lease.bindingGeneration == binding.generation
        && lease.window == binding.window;
}

// the class is registered per module, so match on our own process id
[[nodiscard]] HWND find_raw_input_window() noexcept {
    const DWORD self = GetCurrentProcessId();
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, kRawInputWindowClass, nullptr)) != nullptr) {
        DWORD owner = 0;
        (void)GetWindowThreadProcessId(window, &owner);
        if (owner == self) {
            return window;
        }
    }
    return nullptr;
}

LRESULT CALLBACK raw_window_procedure(HWND window,
                                      UINT message,
                                      WPARAM word,
                                      LPARAM value) noexcept {
    g_rawActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
    AcquireSRWLockShared(&g_rawLock);
    const WNDPROC original = g_rawBinding.window == window ? g_rawBinding.original : nullptr;
    const RawBinding binding = g_rawBinding;
    const RawCaptureClaim claim = g_rawCaptureClaim;
    const RawOpeningLease opening = g_rawOpeningLease;
    ReleaseSRWLockShared(&g_rawLock);

    // captured input still goes to the default procedure or the system keeps the raw buffer alive
    bool nativePauseOwnsInput = false;
#if defined(RESTORATION_NATIVE_RAW_MOUSE_PAUSE) && RESTORATION_NATIVE_RAW_MOUSE_PAUSE
    nativePauseOwnsInput = polled_input::native_pause_owned();
#endif
    const auto visibility = core::ui::runtime::snapshot();
    const bool dynamicCapture = binding.window == window && claim_matches(claim, binding, visibility);
    // without an explicit permission, native-pause forwarding is unchanged; a lease flag alone
    // must not capture before old writers drain
    const bool pendingCapture = binding.window == window && opening_matches(opening, binding);
    const bool captured = message == WM_INPUT && (pendingCapture || (visibility.visible
                          && (!nativePauseOwnsInput || dynamicCapture)));
    const bool forward = !captured && original != nullptr;
    const LRESULT result = forward ? CallWindowProcW(original, window, message, word, value)
                                   : DefWindowProcW(window, message, word, value);
    g_rawActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

} // namespace

bool install_raw_input_window() noexcept {
    AcquireSRWLockExclusive(&g_rawLock);
    if (g_rawBinding.installed) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return true;
    }
    if (g_rawActiveCallbacks.load(std::memory_order_acquire) != 0) {
        // a retired procedure keeps its forwarding record until every old call returns
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    ReleaseSRWLockExclusive(&g_rawLock);

    HWND const window = find_raw_input_window();
    if (window == nullptr) {
        return false;
    }

    AcquireSRWLockExclusive(&g_rawLock);
    if (g_rawBinding.installed) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return true;
    }
    if (g_rawActiveCallbacks.load(std::memory_order_acquire) != 0) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR original =
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&raw_window_procedure));
    if (original == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    g_rawBinding = RawBinding{window, std::bit_cast<WNDPROC>(original), true,
        next_identity_locked(g_rawBindingGeneration)};
    g_rawCaptureClaim = {};
    g_rawOpeningLease = {};
    ReleaseSRWLockExclusive(&g_rawLock);
    return true;
}

bool uninstall_raw_input_window() noexcept {
#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
    if (!cursor::tail::prepare_retirement()) return false;
#endif
    AcquireSRWLockExclusive(&g_rawLock);
    if (!g_rawBinding.installed) {
        const bool idle = g_rawActiveCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_rawLock);
        return idle;
    }
    if (IsWindow(g_rawBinding.window) == FALSE) {
        retire_raw_binding_locked();
        const bool idle = g_rawActiveCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_rawLock);
        return idle;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR current = GetWindowLongPtrW(g_rawBinding.window, GWLP_WNDPROC);
    if (current == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    if (current == reinterpret_cast<LONG_PTR>(g_rawBinding.original)) {
        retire_raw_binding_locked();
        const bool idle = g_rawActiveCallbacks.load(std::memory_order_acquire) == 0;
        ReleaseSRWLockExclusive(&g_rawLock);
        return idle;
    }
    if (current != reinterpret_cast<LONG_PTR>(&raw_window_procedure)) {
        // a later subclass owns the chain head; do not overwrite it
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR replaced = SetWindowLongPtrW(
        g_rawBinding.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_rawBinding.original));
    if (replaced == 0 && GetLastError() != ERROR_SUCCESS) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    retire_raw_binding_locked();
    const bool idle = g_rawActiveCallbacks.load(std::memory_order_acquire) == 0;
    ReleaseSRWLockExclusive(&g_rawLock);
    return idle;
}

RawInputSnapshot snapshot_raw_input() noexcept {
    const LastError error;
    const auto before = core::ui::runtime::snapshot();
    AcquireSRWLockShared(&g_rawLock);
    const RawBinding binding = g_rawBinding;
    const RawCaptureClaim claim = g_rawCaptureClaim;
    const unsigned int activeCount = g_rawActiveCallbacks.load(std::memory_order_acquire);
    ReleaseSRWLockShared(&g_rawLock);
    const auto after = core::ui::runtime::snapshot();
    RawInputSnapshot result{};
    result.visibilityGeneration = after.generation;
    result.bindingGeneration = binding.generation;
    result.window = binding.window;
    result.activeCallbacks = activeCount;
    result.claim = claim;
    result.installed = binding.installed;
    result.visible = after.visible;
    result.stableVisibility = before.generation != 0 && before.generation == after.generation
        && before.visible == after.visible;
    result.claimMatched = result.stableVisibility && claim_matches(claim, binding, after);
    result.drained = result.claimMatched && activeCount == 0;
    return result;
}

bool claim_raw_input_capture(std::uint64_t visibilityGeneration,
    std::uint64_t bindingGeneration, RawCaptureClaim& claim) noexcept {
    const LastError error;
    claim = {};
    if (visibilityGeneration == 0 || bindingGeneration == 0) return false;
    AcquireSRWLockExclusive(&g_rawLock);
    const auto visibility = core::ui::runtime::snapshot();
    if (!visibility.visible || visibility.generation != visibilityGeneration
        || !g_rawBinding.installed || g_rawBinding.generation != bindingGeneration
        || IsWindow(g_rawBinding.window) == FALSE
        || GetWindowLongPtrW(g_rawBinding.window, GWLP_WNDPROC)
            != reinterpret_cast<LONG_PTR>(&raw_window_procedure)) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    if (claim_matches(g_rawCaptureClaim, g_rawBinding, visibility)) {
        claim = g_rawCaptureClaim;
    } else {
        const std::uint64_t identity = next_identity_locked(g_rawClaimIdentity);
        if (identity == 0) {
            ReleaseSRWLockExclusive(&g_rawLock);
            return false;
        }
        g_rawCaptureClaim = {identity, visibilityGeneration, bindingGeneration, g_rawBinding.window};
        claim = g_rawCaptureClaim;
    }
    ReleaseSRWLockExclusive(&g_rawLock);
    return true;
}

bool release_raw_input_capture(const RawCaptureClaim& claim) noexcept {
    const LastError error;
    if (claim.identity == 0) return false;
    AcquireSRWLockExclusive(&g_rawLock);
    const bool matched = g_rawCaptureClaim == claim;
    if (matched) g_rawCaptureClaim = {};
    ReleaseSRWLockExclusive(&g_rawLock);
    return matched;
}

bool claim_raw_opening(std::uint64_t hiddenGeneration, std::uint64_t bindingGeneration,
                       RawOpeningLease& lease) noexcept {
    const LastError error;
    lease = {};
    if (!hiddenGeneration || !bindingGeneration) return false;
    AcquireSRWLockExclusive(&g_rawLock);
    const auto visibility = core::ui::runtime::snapshot();
    const bool permitted = visibility.initialized && visibility.enabled && !visibility.visible
        && visibility.generation == hiddenGeneration && g_rawBinding.installed
        && g_rawBinding.generation == bindingGeneration
        && IsWindow(g_rawBinding.window) != FALSE
        && GetWindowLongPtrW(g_rawBinding.window, GWLP_WNDPROC)
            == reinterpret_cast<LONG_PTR>(&raw_window_procedure);
    if (!permitted || g_rawOpeningLease.identity != 0) {
        ReleaseSRWLockExclusive(&g_rawLock);
        return false;
    }
    const auto identity = next_identity_locked(g_rawClaimIdentity);
    if (identity != 0) {
        g_rawOpeningLease = {identity, hiddenGeneration, bindingGeneration, g_rawBinding.window};
        lease = g_rawOpeningLease;
    }
    ReleaseSRWLockExclusive(&g_rawLock);
    return identity != 0;
}

RawOpeningSnapshot snapshot_raw_opening(const RawOpeningLease& lease) noexcept {
    const LastError error;
    AcquireSRWLockShared(&g_rawLock);
    RawOpeningSnapshot result{};
    result.lease = g_rawOpeningLease;
    result.activeCallbacks = g_rawActiveCallbacks.load(std::memory_order_acquire);
    result.matched = lease.identity != 0 && lease == g_rawOpeningLease
        && opening_matches(lease, g_rawBinding);
    result.drained = result.matched && result.activeCallbacks == 0;
    result.retired = lease.identity != 0 && lease.bindingGeneration != 0
        && (!g_rawBinding.installed || g_rawBinding.generation != lease.bindingGeneration
            || g_rawBinding.window != lease.window);
    ReleaseSRWLockShared(&g_rawLock);
    return result;
}

bool release_raw_opening(const RawOpeningLease& lease) noexcept {
    const LastError error;
    if (!lease.identity) return false;
    AcquireSRWLockExclusive(&g_rawLock);
    const bool matched = lease == g_rawOpeningLease;
    if (matched) g_rawOpeningLease = {};
    ReleaseSRWLockExclusive(&g_rawLock);
    return matched;
}

} // namespace dawn::client::hooks::graphics::input
