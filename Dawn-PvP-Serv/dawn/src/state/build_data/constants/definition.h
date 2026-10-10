#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::constants {

inline constexpr std::size_t kCharacterStatRowCount = 6;
inline constexpr std::size_t kStatRowCount = 64;

// stat rows the client searches by; row 0 is real, so extracted marks presence
struct InvestmentConstants {
    bool extracted{};
    std::uint8_t lightStatRow{};
    std::uint8_t weaponPowerStatRow{};
    std::array<std::uint8_t, kCharacterStatRowCount> characterStatRows{};
};

[[nodiscard]] constexpr bool valid(const InvestmentConstants& value) noexcept {
    return value.extracted && value.weaponPowerStatRow < kStatRowCount;
}

} // namespace dawn::state::build_data::constants
