#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::activity::bubble_authority {

inline constexpr std::size_t kAuthoritySlotCount = 65;
// bubble 64 goes out with the first usable bubble, never derived from a slice set
inline constexpr std::uint8_t kFallbackBubble = 64;
inline constexpr std::int32_t kMaximumGrantSliceSetIndex = 511;
inline constexpr std::uint8_t kSliceSetToBubbleShift = 3;
inline constexpr std::uint16_t kInitialGrantToken = 1;
inline constexpr std::uint8_t kInvalidBubble = 0xFF;

struct Grant final {
    std::uint8_t bubble{kInvalidBubble};
    std::uint16_t token{};
};

struct AuthorityState final {
    std::array<std::uint16_t, kAuthoritySlotCount> grantTokens{};
};

} // namespace dawn::state::activity::bubble_authority
