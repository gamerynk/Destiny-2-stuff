#include "bootflow_hook_lifecycle.h"

#include <atomic>

#include "internal.h"

namespace dawn::client::hooks::bootflow {
namespace {

std::atomic_bool g_installed{false};

} // namespace

bool install() noexcept {
    const bool hold = install_character_select_hold();
    const bool sliceSet = install_orbit_slice_set();
    const bool skip = install_profile_setup_skip();
    const bool composition = install_composition_check();
    const bool towerSearchCompletion = install_tower_search_completion();
    const bool handoff = install_orbit_handoff();
    const bool dedicatedFireteam = install_dedicated_fireteam_bootstrap();
    const bool worldPresence = install_world_presence();
    const bool multiplayer = install_multiplayer_runtime();
    const bool anyFix = hold || sliceSet || skip || composition || handoff || dedicatedFireteam
                        || multiplayer
                        || worldPresence || towerSearchCompletion;
    g_installed.store(anyFix, std::memory_order_release);
    return hold && sliceSet && skip && composition && handoff && dedicatedFireteam
           && multiplayer && worldPresence
           && towerSearchCompletion;
}

bool uninstall() noexcept {
    // movement hooks can refuse detach while a native callback is active; keep the runtime
    // alive and allow a retry instead of claiming it is safe to unload
    if (!uninstall_multiplayer_runtime()) {
        return false;
    }
    uninstall_world_presence();
    uninstall_dedicated_fireteam_bootstrap();
    uninstall_orbit_handoff();
    uninstall_tower_search_completion();
    uninstall_composition_check();
    uninstall_profile_setup_skip();
    uninstall_orbit_slice_set();
    uninstall_character_select_hold();
    g_installed.store(false, std::memory_order_release);
    return true;
}

bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace dawn::client::hooks::bootflow
