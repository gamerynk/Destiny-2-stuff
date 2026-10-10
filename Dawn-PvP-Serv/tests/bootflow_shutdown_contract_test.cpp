#include "../dawn/src/client/hooks/bootflow/bootflow_hook_lifecycle.h"
#include <cstdio>

namespace dawn::client::hooks::bootflow {
bool allowMovementDetach{};
unsigned remainingDetaches{};
#define STUB_HOOK(name) \
    bool install_##name() noexcept { return true; } \
    void uninstall_##name() noexcept { ++remainingDetaches; }
STUB_HOOK(character_select_hold)
STUB_HOOK(orbit_slice_set)
STUB_HOOK(profile_setup_skip)
STUB_HOOK(composition_check)
STUB_HOOK(tower_search_completion)
STUB_HOOK(orbit_handoff)
STUB_HOOK(dedicated_fireteam_bootstrap)
STUB_HOOK(world_presence)
#undef STUB_HOOK
bool install_multiplayer_runtime() noexcept { return true; }
bool uninstall_multiplayer_runtime() noexcept { return allowMovementDetach; }
}

int main() {
    namespace boot = dawn::client::hooks::bootflow;
    if (!boot::install() || !boot::is_installed()) return 1;
    if (boot::uninstall() || !boot::is_installed() || boot::remainingDetaches != 0) {
        std::puts("FAIL: refused movement detach must preserve bootflow and report failure");
        return 1;
    }
    boot::allowMovementDetach = true;
    if (!boot::uninstall() || boot::is_installed() || boot::remainingDetaches != 8) {
        std::puts("FAIL: a later successful retry must finish the remaining cleanup");
        return 1;
    }
    std::puts("PASS: retained movement hooks prevent successful bootflow shutdown; retry succeeds");
    return 0;
}
