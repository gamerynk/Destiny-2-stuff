#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../state/account/account_state.h"

namespace dawn::middleware::datagen::roster {

inline constexpr std::size_t kCharacterCapacity = 10;
inline constexpr std::size_t kEquipmentCapacity = 20;

struct EquipmentReference {
    std::uint16_t definitionIndex{};
    std::uint16_t reserved{};
    std::int32_t value{};
};

struct CharacterEntry {
    std::uint64_t characterSoid{};
    std::array<EquipmentReference, kEquipmentCapacity> equipment{};
};

struct Block {
    std::uint32_t characterCount{};
    std::uint32_t reserved{};
    std::array<CharacterEntry, kCharacterCapacity> characters{};
};

[[nodiscard]] bool initialize(const state::AccountState& account, Block& block) noexcept;

static_assert(sizeof(EquipmentReference) == 2 * sizeof(std::uint16_t) + sizeof(std::int32_t));
static_assert(sizeof(CharacterEntry)
              == sizeof(std::uint64_t) + kEquipmentCapacity * sizeof(EquipmentReference));
static_assert(sizeof(Block)
              == 2 * sizeof(std::uint32_t) + kCharacterCapacity * sizeof(CharacterEntry));

} // namespace dawn::middleware::datagen::roster
