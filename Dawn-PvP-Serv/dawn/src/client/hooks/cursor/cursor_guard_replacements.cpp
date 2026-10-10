#include "cursor_guard_replacements.h"

#include "runtime.h"
#include "native_cursor_tail_runtime.h"

namespace dawn::client::hooks::cursor {
namespace {

SRWLOCK g_policyLock{SRWLOCK_INIT};
bool g_interfaceOpen{};
bool g_gameClipValid{};
RECT g_gameClip{};

} // namespace

std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targets{};

__declspec(noinline) BOOL WINAPI set_cursor_pos(int x, int y) noexcept {
    AcquireSRWLockShared(&g_policyLock);
    const bool open = g_interfaceOpen;
    ReleaseSRWLockShared(&g_policyLock);
    if (open) {
        return kCallSucceeded;
    }

    const SetCursorPos next = original<SetCursorPos>(HookSlot::setCursorPos);
    if (next == nullptr) {
        return kCallSucceeded;
    }
    return next(x, y);
}

__declspec(noinline) BOOL WINAPI clip_cursor(const RECT* bounds) noexcept {
    AcquireSRWLockExclusive(&g_policyLock);
    g_gameClipValid = bounds != nullptr;
    if (g_gameClipValid) {
        g_gameClip = *bounds;
    }
    const bool open = g_interfaceOpen;
    ReleaseSRWLockExclusive(&g_policyLock);

    const ClipCursor next = original<ClipCursor>(HookSlot::clipCursor);
    if (next == nullptr) {
        return kCallSucceeded;
    }
    return next(open ? nullptr : bounds);
}

void apply_policy(bool visible) noexcept {
    AcquireSRWLockExclusive(&g_policyLock);
    if (g_interfaceOpen == visible) {
        ReleaseSRWLockExclusive(&g_policyLock);
        return;
    }
    g_interfaceOpen = visible;
    const bool restore = !visible && g_gameClipValid;
    const RECT bounds = g_gameClip;
    ReleaseSRWLockExclusive(&g_policyLock);

#if defined(RESTORATION_NATIVE_CLIP_OMISSION) && RESTORATION_NATIVE_CLIP_OMISSION
    // once the native tail owns cursor policy only its marker/geometry can restore confinement;
    // renderer publication must not resurrect the old intercepted RECT
    if (tail::manages_policy()) return;
#endif

    const ClipCursor next = original<ClipCursor>(HookSlot::clipCursor);
    if (next == nullptr) {
        return;
    }
    if (visible) {
        (void)next(nullptr);
        return;
    }
    if (restore) {
        (void)next(&bounds);
    }
}

void apply_visibility(bool visible) noexcept {
    apply_policy(visible);
}

void publish_guarded_visibility(bool visible) noexcept {
    AcquireSRWLockExclusive(&g_policyLock);
    g_interfaceOpen = visible;
    ReleaseSRWLockExclusive(&g_policyLock);
}
void restore_guarded_policy(bool visible) noexcept {
    AcquireSRWLockExclusive(&g_policyLock);
    g_interfaceOpen = visible;
    const bool valid = g_gameClipValid;
    const RECT rectangle = g_gameClip;
    ReleaseSRWLockExclusive(&g_policyLock);
    const auto next = original<ClipCursor>(HookSlot::clipCursor);
    if (next) (void)next(!visible && valid ? &rectangle : nullptr);
}

} // namespace dawn::client::hooks::cursor
