#pragma once

#include <span>

#include "../../../../state/account/account_state.h"
#include "../../../../state/equipment/light/definition.h"
#include "../loadout/definition.h"

namespace dawn::middleware::datagen::family4::character {

[[nodiscard]] bool encode(const state::CharacterState& state,
                          const loadout::ResolvedLoadout& resolvedLoadout,
                          const state::equipment::light::Evaluation& lightEvaluation,
                          std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::datagen::family4::character
