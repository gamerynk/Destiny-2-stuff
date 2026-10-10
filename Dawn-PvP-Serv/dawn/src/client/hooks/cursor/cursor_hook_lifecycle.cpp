#include <Windows.h>

#include <array>
#include <atomic>

#include "../../../core/logging/log.h"
#include "cursor_guard_replacements.h"
#include "native_cursor_tail_runtime.h"
#include "runtime.h"
#include "../polled_input/runtime.h"

#ifndef RESTORATION_NATIVE_SET_CURSOR_OMISSION
#define RESTORATION_NATIVE_SET_CURSOR_OMISSION 0
#endif
#ifndef RESTORATION_NATIVE_CLIP_OMISSION
#define RESTORATION_NATIVE_CLIP_OMISSION 0
#endif

namespace dawn::client::hooks::cursor {
namespace {

// System32 name keeps the guard from finding exports in the game directory
constexpr wchar_t kUserModule[] = L"user32.dll";
constexpr char kSetCursorPosExport[] = "SetCursorPos";
constexpr char kClipCursorExport[] = "ClipCursor";

std::atomic_bool g_installed{false};
std::atomic_bool g_clipRecovery{}, g_clipAttached{};
SRWLOCK g_cursorHookLock{SRWLOCK_INIT};
HMODULE g_module{};

void release_module() noexcept {
    if (g_module != nullptr) {
        (void)FreeLibrary(g_module);
        g_module = nullptr;
    }
}

} // namespace

bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    g_module = LoadLibraryExW(kUserModule, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_module == nullptr) {
        return false;
    }

    g_targets[static_cast<std::size_t>(HookSlot::setCursorPos)] =
        reinterpret_cast<void*>(GetProcAddress(g_module, kSetCursorPosExport));
    g_targets[static_cast<std::size_t>(HookSlot::clipCursor)] =
        reinterpret_cast<void*>(GetProcAddress(g_module, kClipCursorExport));
    for (void* target : g_targets) {
        if (target == nullptr) {
            g_targets = {};
            release_module();
            return false;
        }
    }

    const std::array specs{
        hooking::detour::Spec{g_targets[static_cast<std::size_t>(HookSlot::setCursorPos)],
                              reinterpret_cast<void*>(&set_cursor_pos)},
        hooking::detour::Spec{g_targets[static_cast<std::size_t>(HookSlot::clipCursor)],
                              reinterpret_cast<void*>(&clip_cursor)},
    };
    // every slot needs its spec, or the transaction silently attaches fewer hooks than it has
    static_assert(specs.size() == kHandleCount);
    // explicit live experiment only; ClipCursor stays guarded because native window confinement
    // still runs during a paused overlay
    const bool omitMove = RESTORATION_NATIVE_SET_CURSOR_OMISSION != 0
        && polled_input::native_pause_available();
    const auto first = omitMove ? std::size_t{1} : std::size_t{0};
    if (!hooking::detour::install(std::span{specs}.subspan(first),
                                 std::span{g_handles}.subspan(first))) {
        g_targets = {};
        release_module();
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=cursor stage=install result=fail");
        return false;
    }

    g_installed.store(true, std::memory_order_release);
    g_clipAttached.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client, core::log::Level::info,
        omitMove ? "ev=cursor stage=install result=ok hooks=1 move=native_pause_trial clip=guarded"
                 : "ev=cursor stage=install result=ok hooks=2 move=guarded clip=guarded");
    return true;
}

void request_native_clip_guard() noexcept { g_clipRecovery.store(true, std::memory_order_release); }
bool native_clip_guard_attached() noexcept { return g_clipAttached.load(std::memory_order_acquire); }
bool prepare_native_tail_shutdown() noexcept {
#if RESTORATION_NATIVE_CLIP_OMISSION
    return tail::prepare_shutdown();
#else
    return true;
#endif
}
bool prepare_native_tail_retirement() noexcept {
#if RESTORATION_NATIVE_CLIP_OMISSION
    return tail::prepare_retirement();
#else
    return true;
#endif
}

void service_native_cursor_tail() noexcept {
#if RESTORATION_NATIVE_CLIP_OMISSION
    if (!g_installed.load(std::memory_order_acquire)) return;
    tail::bootstrap();
    if (!tail::configured() || !polled_input::native_key_recovery_safe()) return;
    AcquireSRWLockExclusive(&g_cursorHookLock);
    if (!g_installed.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_cursorHookLock); return;
    }
    auto handles = std::span{g_handles}.subspan(static_cast<std::size_t>(HookSlot::clipCursor), 1);
    const bool attached = handles.front().attached;
    if (g_clipRecovery.load(std::memory_order_acquire)) {
        if (!attached) {
            const std::array spec{hooking::detour::Spec{
                g_targets[static_cast<std::size_t>(HookSlot::clipCursor)],
                reinterpret_cast<void*>(&clip_cursor)}};
            const bool restored = hooking::detour::install(std::span{spec}, handles);
            g_clipAttached.store(restored, std::memory_order_release);
            core::log::write(core::log::Channel::client, restored ? core::log::Level::info
                : core::log::Level::warn, restored
                ? "ev=cursor stage=native_tail result=fallback_attached clip_hooks=1"
                : "ev=cursor stage=native_tail result=fallback_pending");
        }
        // recovery is sticky for this runtime: never discard a real fallback after contract loss
    } else if (attached && tail::omission_ready()) {
        const bool removed = hooking::detour::uninstall(handles);
        g_clipAttached.store(!removed, std::memory_order_release);
        core::log::write(core::log::Channel::client, removed ? core::log::Level::info
            : core::log::Level::warn, removed
            ? "ev=cursor stage=native_tail result=selected clip_hooks=0 capture=pre_visible"
            : "ev=cursor stage=native_tail result=detach_pending clip_hooks=1");
    }
    ReleaseSRWLockExclusive(&g_cursorHookLock);
#endif
}

void uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        release_module();
        return;
    }
    if (!prepare_native_tail_shutdown()) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         "ev=cursor stage=uninstall result=pending reason=owner_tail");
        return;
    }
    apply_policy(false);
    AcquireSRWLockExclusive(&g_cursorHookLock);
    if (!hooking::detour::uninstall(g_handles)) {
        ReleaseSRWLockExclusive(&g_cursorHookLock);
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=cursor stage=uninstall result=fail");
        return;
    }
    g_installed.store(false, std::memory_order_release);
    g_clipAttached.store(false, std::memory_order_release);
    g_targets = {};
    release_module();
#if RESTORATION_NATIVE_CLIP_OMISSION
    tail::reset_after_uninstall();
#endif
    g_clipRecovery.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_cursorHookLock);
}

bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace dawn::client::hooks::cursor
