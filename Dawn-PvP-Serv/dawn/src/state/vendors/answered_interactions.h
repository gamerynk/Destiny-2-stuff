#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::state::vendors {

// the vendor picker skips interactions its retire test calls answered, and offline nothing
// appends to its list, so this is Dawn's copy. In State: hook and server both need it

inline constexpr std::size_t kVendorCapacity = 512;

inline constexpr std::size_t kAnsweredCapacity = 256;

inline constexpr std::uint16_t kAbsentIndex = 0xFFFFU;

void record_shown(std::uint16_t vendorIndex, std::uint16_t interactionIndex) noexcept;

[[nodiscard]] bool is_answered(std::uint16_t vendorIndex, std::uint16_t interactionIndex) noexcept;

// called once a quest grant commits, which is when the shipped game appends its own entry
bool answer_shown(std::uint16_t vendorIndex) noexcept;

void clear() noexcept;

} // namespace dawn::state::vendors
