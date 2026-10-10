#pragma once

#include <cstddef>

#include "../../../../state/account/account_state.h"
#include "definition.h"

namespace dawn::middleware::datagen::family4::loadout {

[[nodiscard]] bool resolve(const state::AccountState& account,
                           std::size_t selectedCharacterIndex,
                           ResolvedLoadout& output) noexcept;

// the equip-summary reader looks up an instance without a null check, so every character the
// roster names needs its records even when its loadout is never encoded
[[nodiscard]] bool resolve_instances(const state::AccountState& account,
                                     std::size_t characterIndex,
                                     ResolvedInstances& output) noexcept;

// family four must publish an instance object for every SOID the character inventory references
[[nodiscard]] bool resolve_owned_instances(const state::AccountState& account,
                                           std::size_t characterIndex,
                                           ResolvedInstances& output) noexcept;

} // namespace dawn::middleware::datagen::family4::loadout
