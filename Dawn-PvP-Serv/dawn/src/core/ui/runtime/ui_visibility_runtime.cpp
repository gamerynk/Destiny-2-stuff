#include "ui_visibility_runtime.h"

#include <Windows.h>

namespace dawn::core::ui::runtime {
namespace {

constexpr UINT kFirstVirtualKey = 0x01;
// Windows reserves 0xFF, so 0xFE is the last usable virtual-key code
constexpr UINT kLastVirtualKey = 0xFE;
constexpr bool kInitialVisibility = false;

VisibilitySnapshot g_state{};
std::uint64_t g_visibilityGeneration{};
SRWLOCK g_visibilityLock{SRWLOCK_INIT};

std::uint64_t next_generation_locked() noexcept {
    // on exhaustion disable generation-qualified consumers rather than wrap to an old ID
    if (g_visibilityGeneration == UINT64_MAX) return 0;
    return ++g_visibilityGeneration;
}

[[nodiscard]] bool is_valid_virtual_key(UINT virtualKey) noexcept {
    return virtualKey >= kFirstVirtualKey && virtualKey <= kLastVirtualKey;
}

} // namespace

bool initialize(const Settings& settings) noexcept {
    if (!is_valid_virtual_key(settings.toggleVirtualKey)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_visibilityLock);
    g_state.initialized = true;
    g_state.enabled = settings.enabled;
    g_state.visible = kInitialVisibility;
    g_state.toggleVirtualKey = settings.toggleVirtualKey;
    g_state.generation = next_generation_locked();
    ReleaseSRWLockExclusive(&g_visibilityLock);
    return true;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_visibilityLock);
    g_state = {};
    g_state.generation = next_generation_locked();
    ReleaseSRWLockExclusive(&g_visibilityLock);
}

VisibilitySnapshot snapshot() noexcept {
    AcquireSRWLockShared(&g_visibilityLock);
    const VisibilitySnapshot result = g_state;
    ReleaseSRWLockShared(&g_visibilityLock);
    return result;
}

bool toggle_for_key(UINT virtualKey) noexcept {
    AcquireSRWLockExclusive(&g_visibilityLock);
    const bool handled =
        g_state.initialized && g_state.enabled && virtualKey == g_state.toggleVirtualKey;
    if (handled) {
        g_state.visible = !g_state.visible;
        g_state.generation = next_generation_locked();
    }
    ReleaseSRWLockExclusive(&g_visibilityLock);
    return handled;
}

bool open_for_key(UINT virtualKey, std::uint64_t hiddenGeneration,
                  VisibilitySnapshot& opened) noexcept {
    opened = {};
    AcquireSRWLockExclusive(&g_visibilityLock);
    const bool permitted = hiddenGeneration != 0 && g_state.generation == hiddenGeneration
        && g_state.initialized && g_state.enabled && !g_state.visible
        && virtualKey == g_state.toggleVirtualKey && g_visibilityGeneration != UINT64_MAX;
    if (permitted) {
        g_state.visible = true;
        g_state.generation = next_generation_locked();
        opened = g_state;
    }
    ReleaseSRWLockExclusive(&g_visibilityLock);
    return permitted;
}

bool close_generation(std::uint64_t visibleGeneration, VisibilitySnapshot& closed) noexcept {
    closed = {};
    AcquireSRWLockExclusive(&g_visibilityLock);
    const bool permitted = visibleGeneration != 0 && g_state.generation == visibleGeneration
        && g_state.initialized && g_state.visible && g_visibilityGeneration != UINT64_MAX;
    if (permitted) {
        g_state.visible = false;
        g_state.generation = next_generation_locked();
        closed = g_state;
    }
    ReleaseSRWLockExclusive(&g_visibilityLock);
    return permitted;
}

} // namespace dawn::core::ui::runtime
