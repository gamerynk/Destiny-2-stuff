#pragma once

#include <cstddef>
#include <cstdint>

#include "../../../../state/account/account_state.h"
#include "../../../../state/account/inventory/inventory_state.h"
#include "../../../../state/build_data/inventory/buckets/definition.h"
#include "definition.h"

namespace dawn::middleware::datagen::family4::loadout {

[[nodiscard]] bool weapon_picker_curated_roll_self_test(std::uint32_t defaultHash,
                                                       std::uint32_t replacementHash) noexcept;

struct Candidate {
    ResolvedItem item{};
    state::build_data::inventory::buckets::Descriptor bucket{};
};

[[nodiscard]] bool resolve_item(const state::account::inventory::Item& authored,
                                const state::CharacterState& character,
                                std::size_t itemDefinitionCount,
                                std::size_t socketEntryListCount,
                                Candidate& output) noexcept;

} // namespace dawn::middleware::datagen::family4::loadout
