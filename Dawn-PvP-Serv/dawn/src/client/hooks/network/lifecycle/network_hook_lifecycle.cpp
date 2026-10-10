#include <atomic>
#include "../../../../core/logging/log.h"
#include "../../external_server/route.h"
#include "../content_config/runtime.h"
#include "../coordinator/network_call_coordinator.h"
#include "../investment/investment_derived_rebuild.h"
#include "../platform.h"
#include "../runtime.h"
#include "network_hook_entries.h"
#include "network_hook_group.h"

namespace dawn::client::hooks::network {
namespace {

// one second caps the cleanup wait so we never spin on game-owned work
constexpr DWORD kQuiesceTimeoutMilliseconds = 1000;
bool g_platformStarted{}, g_platformStopping{};
std::atomic_flag g_platformTransition = ATOMIC_FLAG_INIT;
struct Transition final {
    bool acquired = !g_platformTransition.test_and_set(std::memory_order_acquire);
    ~Transition() { if (acquired) g_platformTransition.clear(std::memory_order_release); }
};

[[nodiscard]] bool wait_for_base_idle() noexcept {
    return coordinator::wait_for_idle(kQuiesceTimeoutMilliseconds);
}

void disable_group(std::span<const HookSlot> slots) noexcept {
    lifecycle::set_accepting(slots, false);
}

[[nodiscard]] bool platform_ready() noexcept {
    const auto slots = lifecycle::platform_slots();
    return slots.empty() || (lifecycle::all_installed(slots) && lifecycle::all_accepting(slots));
}

} // namespace

bool install_game() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (content_config::is_installed() && external_server::is_installed()
        && investment::is_installed()) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if ((investment::has_ownership() && !investment::is_installed())
        || (external_server::has_ownership() && !external_server::is_installed())
        || (content_config::has_ownership() && !content_config::is_installed())) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    const bool installedContent = content_config::install();
    const bool installedRoute = installedContent && external_server::install();
    const bool installed = installedRoute && investment::install();
    if (!installed) {
        (void)investment::uninstall();
        const bool routeRemoved = external_server::uninstall();
        if (routeRemoved && content_config::has_ownership()) {
            (void)content_config::uninstall();
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return installed;
}

bool install_platform() noexcept {
    Transition transition{};
    if (!transition.acquired) return false;
    AcquireSRWLockExclusive(&g_lock);
    g_platformStopping = false;
    if (lifecycle::platform_slots().empty()) {
        g_platformStarted = true;
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (lifecycle::all_installed(lifecycle::platform_slots())) {
        const bool accepting = lifecycle::all_accepting(lifecycle::platform_slots());
        ReleaseSRWLockExclusive(&g_lock);
        return accepting;
    }
    if (lifecycle::any_installed(lifecycle::platform_slots())) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    const lifecycle::PlatformSpecs specs = lifecycle::platform_specs();
    const bool installed = lifecycle::install_group(std::span<const hooking::detour::Spec>(specs).first(lifecycle::platform_slots().size()), lifecycle::platform_slots());
    g_platformStarted = installed;
    ReleaseSRWLockExclusive(&g_lock);
    return installed;
}

bool service_platform_policy() noexcept {
    // process-latched trials deliberately bypass adaptive compatibility ownership
    if (platform::native_certificate_requested() || platform::native_authentication_requested()
        || coordinator::current_thread_active()) return true;
    Transition transition{};
    if (!transition.acquired) return false;
    AcquireSRWLockShared(&g_lock);
    const bool started = g_platformStarted && !g_platformStopping;
    ReleaseSRWLockShared(&g_lock);
    if (!started) return true;
    const bool healthy = platform::automatic_native_ready(); // original call lease, no lifecycle lock
    AcquireSRWLockExclusive(&g_lock);
    if (!g_platformStarted || g_platformStopping) { ReleaseSRWLockExclusive(&g_lock); return true; }
    const bool selected = platform::automatic_native_selected();
    if (selected == healthy) { ReleaseSRWLockExclusive(&g_lock); return true; }
    if (!healthy) {
        // restore the whole fallback group before a failed or new certificate callback is delivered
        const auto specs = lifecycle::platform_specs();
        const bool installed = lifecycle::install_group(specs, lifecycle::kPlatformSlots);
        if (installed) platform::select_automatic_native(false);
        ReleaseSRWLockExclusive(&g_lock);
        core::log::write(core::log::Channel::client, installed ? core::log::Level::info : core::log::Level::warn,
            installed ? "ev=steam_certificate stage=adaptive policy=compatibility active_detours=2 startup_peak=2"
                      : "ev=steam_certificate stage=adaptive result=deferred reason=fallback_attach active_detours=0 startup_peak=2");
        return installed;
    }
    disable_group(lifecycle::kPlatformSlots);
    ReleaseSRWLockExclusive(&g_lock);
    const bool idle = wait_for_base_idle();
    AcquireSRWLockExclusive(&g_lock);
    const bool removed = idle && coordinator::active_calls() == 0
        && lifecycle::uninstall_group(lifecycle::kPlatformSlots, lifecycle::platform_protected_entries());
    if (removed) platform::select_automatic_native(true);
    else lifecycle::set_accepting(lifecycle::kPlatformSlots, true);
    ReleaseSRWLockExclusive(&g_lock);
    core::log::write(core::log::Channel::client, removed ? core::log::Level::info : core::log::Level::warn,
        removed ? "ev=steam_certificate stage=adaptive policy=native active_detours=0 startup_peak=2 durable=1 key_match=1 native_auth=100"
                : "ev=steam_certificate stage=adaptive policy=compatibility active_detours=2 startup_peak=2 reason=quiescence_or_detach");
    return true;
}

bool uninstall() noexcept {
    if (coordinator::current_thread_active()) {
        return false;
    }

    Transition transition{};
    if (!transition.acquired) return false;
    AcquireSRWLockExclusive(&g_lock);
    g_platformStopping = true;
    disable_group(lifecycle::platform_slots());
    ReleaseSRWLockExclusive(&g_lock);
    if (!wait_for_base_idle()) {
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    const auto platformProtectedEntries = lifecycle::platform_protected_entries();
    if (coordinator::active_calls() != 0
        || !lifecycle::uninstall_group(lifecycle::platform_slots(), platformProtectedEntries)) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_platformStarted = false;
    platform::select_automatic_native(false);
    ReleaseSRWLockExclusive(&g_lock);

    if (!external_server::uninstall()) {
        return false;
    }
    return investment::uninstall() && content_config::uninstall();
}

bool is_game_installed() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool installed = content_config::is_installed() && external_server::is_installed()
                           && investment::is_installed();
    ReleaseSRWLockShared(&g_lock);
    return installed;
}

bool has_game_ownership() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool owned = content_config::has_ownership() || external_server::has_ownership()
                       || investment::has_ownership();
    ReleaseSRWLockShared(&g_lock);
    return owned;
}

bool is_platform_installed() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool installed = platform_ready();
    ReleaseSRWLockShared(&g_lock);
    return installed;
}

bool is_installed() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool installed = platform_ready()
                           && content_config::is_installed() && external_server::is_installed()
                           && investment::is_installed();
    ReleaseSRWLockShared(&g_lock);
    return installed;
}

} // namespace dawn::client::hooks::network
