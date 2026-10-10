#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/socket_entry_lists/definition.h"
#include "../instance/instance_encoder.h"

namespace dawn::middleware::datagen::family4::loadout {

inline constexpr std::size_t kSelectorBucketCount = 12;
inline constexpr std::size_t kSelectedEntryCount = 6;

struct SelectedEntry {
    std::uint8_t entry{};
    std::uint8_t bucket{};
};

struct SubclassSelection {
    std::array<SelectedEntry, kSelectedEntryCount> selected{};
};

void subclass_selection(const state::CharacterState& character, SubclassSelection& output) noexcept;

// only a list with a super lane belongs to a subclass; every other item keeps its absent and
// ready states and publishes no selector
void resolve_socket_states(
    const state::build_data::socket_entry_lists::Definition& definition,
    const state::CharacterState& character,
    std::array<instance::SocketEntryState, instance::layout::kSocketEntryStateCapacity>& output,
    std::array<instance::SocketSelector, kSelectorBucketCount>& selectors) noexcept;

} // namespace dawn::middleware::datagen::family4::loadout
