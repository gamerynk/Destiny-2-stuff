#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "internal.h"
#include "entity_schedule_diagnostic.h"
#include "weapon_frame_wake.h"
#include "special_ammo_handoff.h"
#include "../../crucible/native_match_ui.h"
#include "remote_facing_control.h"

#ifndef RESTORATION_NATIVE_MATCH_PRESENTATION
#define RESTORATION_NATIVE_MATCH_PRESENTATION 0
#endif

namespace dawn::client::hooks::bootflow {

bool install_multiplayer_runtime() noexcept {
    entity_schedule_diagnostic::install();
    weapon_frame_wake::install();
    special_ammo_handoff::install();
#if RESTORATION_NATIVE_MATCH_PRESENTATION
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=native_match_ui stage=install result=native detours=0 paired_authority_and_assets_required=1");
#else
    crucible::native_ui::install();
#endif
    remote_facing_control::install();
    return true;
}

bool uninstall_multiplayer_runtime() noexcept {
    const bool weaponRemoved = weapon_frame_wake::uninstall();
    const bool pickupRemoved = special_ammo_handoff::uninstall();
#if RESTORATION_NATIVE_MATCH_PRESENTATION
    const bool matchUiRemoved = true;
#else
    const bool matchUiRemoved = crucible::native_ui::uninstall();
#endif
    const bool facingRemoved = remote_facing_control::uninstall();
    const bool movementRemoved = entity_schedule_diagnostic::uninstall();
    return weaponRemoved && pickupRemoved && matchUiRemoved
        && facingRemoved && movementRemoved;
}

} // namespace dawn::client::hooks::bootflow
