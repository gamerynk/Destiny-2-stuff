#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::activity::entity_slots {

inline constexpr std::size_t kSlotCount = 8'192;
inline constexpr std::size_t kSlotsPerByte = 8;
inline constexpr std::size_t kMaskSize = kSlotCount / kSlotsPerByte;

using LeaseMask = std::array<std::byte, kMaskSize>;

enum class MutationKind : std::uint8_t {
    none,
    join,
    grant,
    release,
};

} // namespace dawn::state::activity::entity_slots
