#pragma once

#include <cstdint>

#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/abilities/definition.h"
#include "../../../../state/build_data/items/details/definition.h"
#include "../../family4/loadout/definition.h"
#include "../layout.h"

namespace dawn::middleware::datagen::character_record::appearance {

namespace details = state::build_data::items::details;

inline constexpr std::int8_t kFirstWeaponSlot = 7;
inline constexpr std::size_t kWeaponSlotCount = 3;
inline constexpr std::uint8_t kSubclassEquipmentSlot = 0;

struct Equipped {
    std::uint8_t equipmentSlot{};
    std::uint16_t definitionIndex{details::kUnavailableItemIndex};
    std::array<std::uint16_t, details::kInitialPlugCapacity> plugs{};
    std::size_t laneCount{};
};

[[nodiscard]] constexpr std::uint16_t
select_art_arrangement(const details::Definition& detail,
                       state::CharacterClass characterClass) noexcept {
    const std::size_t classSlot = static_cast<std::size_t>(characterClass) + 1U;
    if (classSlot < detail.artArrangementIndices.size()
        && detail.artArrangementIndices[classSlot] != details::kUnavailableArtIndex) {
        return detail.artArrangementIndices[classSlot];
    }
    return detail.artArrangementIndices.front();
}

// resolver already applied the socket policy, so an unavailable lane holds nothing
[[nodiscard]] bool resolve_equipped(const family4::loadout::SlottedInstance& slotted,
                                    details::Definition& detail,
                                    Equipped& equipped) noexcept;

void apply_sentinels(layout::Appearance& appearance) noexcept;

[[nodiscard]] bool apply_render(const family4::loadout::ResolvedInstances& instances,
                                state::CharacterClass characterClass,
                                layout::Appearance& appearance) noexcept;

[[nodiscard]] bool apply_ability_buckets(const state::CharacterState& character,
                                         const family4::loadout::ResolvedInstances& instances,
                                         layout::Appearance& appearance) noexcept;

// runs after the ability buckets, which own whatever overflow slots the subclass claims
void apply_overflow_hashes(const family4::loadout::ResolvedInstances& instances,
                           layout::Appearance& appearance) noexcept;

void apply_perk_banks(const state::CharacterState& character,
                      const family4::loadout::ResolvedInstances& instances,
                      layout::Appearance& appearance) noexcept;

[[nodiscard]] bool apply_stats(const family4::loadout::ResolvedInstances& instances,
                               std::int32_t light,
                               layout::Appearance& appearance) noexcept;

} // namespace dawn::middleware::datagen::character_record::appearance
