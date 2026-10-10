#pragma once

#include "../../patterns/image_scan.h"

namespace dawn::client::hooks::bootflow {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

[[nodiscard]] bool install_dedicated_fireteam_bootstrap() noexcept;
[[nodiscard]] bool prearm_dedicated_fireteam_bootstrap() noexcept;
void uninstall_dedicated_fireteam_bootstrap() noexcept;

[[nodiscard]] bool install_character_select_hold() noexcept;
void uninstall_character_select_hold() noexcept;

[[nodiscard]] bool install_profile_setup_skip() noexcept;
void uninstall_profile_setup_skip() noexcept;

[[nodiscard]] bool install_orbit_slice_set() noexcept;
void uninstall_orbit_slice_set() noexcept;

[[nodiscard]] bool install_composition_check() noexcept;
void uninstall_composition_check() noexcept;

[[nodiscard]] bool install_tower_search_completion() noexcept;
void uninstall_tower_search_completion() noexcept;

[[nodiscard]] bool install_orbit_handoff() noexcept;
void uninstall_orbit_handoff() noexcept;

[[nodiscard]] bool install_multiplayer_runtime() noexcept;
bool uninstall_multiplayer_runtime() noexcept;

[[nodiscard]] bool install_world_presence() noexcept;
void uninstall_world_presence() noexcept;

} // namespace dawn::client::hooks::bootflow
