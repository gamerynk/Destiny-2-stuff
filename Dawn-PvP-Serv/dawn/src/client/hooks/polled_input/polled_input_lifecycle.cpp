#include <Windows.h>

#include <array>
#include <atomic>

#include "../../../core/logging/log.h"
#include "polled_input_replacements.h"
#include "runtime.h"

#ifndef RESTORATION_NATIVE_ASYNC_OMISSION
#define RESTORATION_NATIVE_ASYNC_OMISSION 0
#endif

namespace dawn::client::hooks::polled_input {
namespace {

// load from System32 so exports are never taken from the game directory
constexpr wchar_t kUserModule[] = L"user32.dll";
constexpr char kGetKeyStateExport[] = "GetKeyState";
constexpr char kGetAsyncKeyStateExport[] = "GetAsyncKeyState";

std::atomic_bool g_installed{false};
HMODULE g_module{};
SRWLOCK g_keyGuardLock = SRWLOCK_INIT;
std::atomic_bool g_keyGuardActive{};
std::atomic_bool g_keyRecoveryRequested{};

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
    if (!resolve_game_range()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=polled_input stage=install result=fail reason=image");
        return false;
    }
    g_module = LoadLibraryExW(kUserModule, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_module == nullptr) {
        clear_game_range();
        return false;
    }

    g_targets[static_cast<std::size_t>(HookSlot::getKeyState)] =
        reinterpret_cast<void*>(GetProcAddress(g_module, kGetKeyStateExport));
    g_targets[static_cast<std::size_t>(HookSlot::getAsyncKeyState)] =
        reinterpret_cast<void*>(GetProcAddress(g_module, kGetAsyncKeyStateExport));
    for (void* target : g_targets) {
        if (target == nullptr) {
            g_targets = {};
            clear_game_range();
            release_module();
            return false;
        }
    }

    const std::array specs{
        hooking::detour::Spec{g_targets[static_cast<std::size_t>(HookSlot::getKeyState)],
                              reinterpret_cast<void*>(&get_key_state)},
        hooking::detour::Spec{g_targets[static_cast<std::size_t>(HookSlot::getAsyncKeyState)],
                              reinterpret_cast<void*>(&get_async_key_state)},
    };
    // every slot needs a spec or the transaction silently attaches fewer hooks than it has
    static_assert(specs.size() == kHandleCount);
    // each omission needs its own validated native contract; a failed key-focus contract
    // keeps the real synchronous guard
    const bool omitKey = native_key_focus_available();
    const bool omitAsync = RESTORATION_NATIVE_ASYNC_OMISSION != 0
        && native_pause_available();
    const std::size_t first = omitKey ? 1 : 0;
    const std::size_t last = omitAsync ? 1 : specs.size();
    const std::size_t count = first < last ? last - first : 0;
    if (count != 0 && !hooking::detour::install(std::span{specs}.subspan(first, count),
                                             std::span{g_handles}.subspan(first, count))) {
        g_targets = {};
        clear_game_range();
        release_module();
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=polled_input stage=install result=fail reason=attach");
        return false;
    }

    g_installed.store(true, std::memory_order_release);
    g_keyGuardActive.store(g_handles[static_cast<std::size_t>(HookSlot::getKeyState)].attached,
                          std::memory_order_release);
    g_keyRecoveryRequested.store(false, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     omitKey && omitAsync
                         ? "ev=polled_input stage=install result=ok hooks=0 keyboard=native_focus_trial async=native_pause_trial"
                     : omitKey
                         ? "ev=polled_input stage=install result=ok hooks=1 keyboard=native_focus_trial async=guarded"
                     : omitAsync
                         ? "ev=polled_input stage=install result=ok hooks=1 keyboard=guarded async=native_pause_trial"
                         : "ev=polled_input stage=install result=ok hooks=2 keyboard=guarded async=guarded");
    return true;
}

void request_native_key_guard() noexcept {
    g_keyRecoveryRequested.store(true, std::memory_order_release);
}

bool native_key_guard_attached() noexcept {
    return g_keyGuardActive.load(std::memory_order_acquire);
}

void service_native_key_recovery() noexcept {
    if (!g_keyRecoveryRequested.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_keyGuardLock);
    bool attached = g_handles[static_cast<std::size_t>(HookSlot::getKeyState)].attached;
    if (!attached && g_installed.load(std::memory_order_acquire)
        && native_key_recovery_safe()) {
        const std::array spec{hooking::detour::Spec{
            g_targets[static_cast<std::size_t>(HookSlot::getKeyState)],
            reinterpret_cast<void*>(&get_key_state)}};
        attached = hooking::detour::install(std::span{spec}, std::span{g_handles}.first(1));
        g_keyGuardActive.store(attached, std::memory_order_release);
        core::log::write(core::log::Channel::client, attached ? core::log::Level::info
            : core::log::Level::warn, attached
                ? "ev=polled_input stage=native_key_focus result=fallback_attached hooks=1"
                : "ev=polled_input stage=native_key_focus result=fallback_pending");
    }
    if (attached) g_keyRecoveryRequested.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_keyGuardLock);
}

void uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        release_module();
        return;
    }
    // reads go back to the real state before the trampoline that serves them is gone
    if (!drain_native_key_state()) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         "ev=polled_input stage=uninstall result=pending reason=keyboard_owner_release");
        return;
    }
    apply_policy(false);
    AcquireSRWLockExclusive(&g_keyGuardLock);
    if (!hooking::detour::uninstall(g_handles)) {
        ReleaseSRWLockExclusive(&g_keyGuardLock);
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=polled_input stage=uninstall result=fail");
        return;
    }
    g_installed.store(false, std::memory_order_release);
    g_keyGuardActive.store(false, std::memory_order_release);
    g_keyRecoveryRequested.store(false, std::memory_order_release);
    g_targets = {};
    clear_game_range();
    release_module();
    ReleaseSRWLockExclusive(&g_keyGuardLock);
}

bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace dawn::client::hooks::polled_input
