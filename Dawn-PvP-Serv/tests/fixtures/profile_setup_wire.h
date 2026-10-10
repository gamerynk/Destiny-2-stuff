#pragma once
#include <array>
#include <cstddef>
namespace settings_wire_fixture {
inline constexpr std::array<std::byte, 11> setup_complete{std::byte{0x5C}};
inline constexpr std::array<std::byte, 11> setup_incomplete{std::byte{0x58}};
inline constexpr std::array<std::byte, 1> setup_absent{std::byte{0}};
}

