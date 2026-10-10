#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../family4/loadout/definition.h"
#include "../family4/progression/layout.h"

namespace dawn::middleware::datagen::family1 {

inline constexpr std::size_t kRootSize = 0x800;
inline constexpr std::size_t kBodySize = 0x1B48;
inline constexpr std::size_t kAuxiliarySize = 0xB08;

inline constexpr std::size_t kProgressionCapacity = 127;
inline constexpr std::size_t kInventoryCapacity = 20;
inline constexpr std::size_t kEquipmentCapacity = 20;

[[nodiscard]] bool encode_root(
    std::uint64_t accountSoid,
    std::uint64_t characterSoid,
    const std::array<family4::progression::layout::Entry, kProgressionCapacity>& progressions,
    std::span<std::byte> output) noexcept;

// resident SOID at +0 is the account/root key the QueueZ slot-one header requires; the selected
// character link stays in the Family-1 root at +8
[[nodiscard]] bool encode_body(
    std::uint64_t residentSoid,
    std::span<const std::byte> identity,
    const std::array<family4::progression::layout::Entry, kProgressionCapacity>& progressions,
    const family4::loadout::ResolvedLoadout& loadout,
    std::span<const std::byte> appearance,
    std::span<const std::byte> summary,
    std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::datagen::family1
