#pragma once

#include <array>
#include <cstddef>

namespace dawn::core::settings::steam {

inline constexpr std::size_t kMaximumPersonaNameBytes = 63;
inline constexpr std::size_t kPersonaNameCapacity = kMaximumPersonaNameBytes + 1;
inline constexpr std::size_t kMaximumLanguageBytes = 15;
inline constexpr std::size_t kLanguageCapacity = kMaximumLanguageBytes + 1;

struct User {
    std::array<char, kPersonaNameCapacity> personaName{"Player"};
};

struct Settings {
    User user;
    std::array<char, kLanguageCapacity> language{"english"};
};

} // namespace dawn::core::settings::steam
