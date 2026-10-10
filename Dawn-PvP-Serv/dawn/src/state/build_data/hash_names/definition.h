#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::hash_names {

inline constexpr std::size_t kNameCapacity = 4'096;
inline constexpr std::size_t kNameLength = 48;

struct Name {
    std::uint32_t hash{};
    std::array<char, kNameLength> name{};
    std::uint8_t nameLength{};
};

[[nodiscard]] constexpr bool name_character(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
}

} // namespace dawn::state::build_data::hash_names
