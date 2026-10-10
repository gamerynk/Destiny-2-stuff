#pragma once
#include <array>
#include <cstddef>
// sparse ws-701: only native seed version and FOV present
namespace settings_wire_fixture {
inline constexpr std::array<std::byte, 11> fov_105{{std::byte{0x4E},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x04},std::byte{0xC0},std::byte{0x00},std::byte{0x00},std::byte{0x34},std::byte{0x80},std::byte{0x00}}};
inline constexpr std::array<std::byte, 11> fov_55{{std::byte{0x4E},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x04},std::byte{0xC0},std::byte{0x00},std::byte{0x00},std::byte{0x1B},std::byte{0x80},std::byte{0x00}}};
inline constexpr std::array<std::byte, 11> invalid_fov{{std::byte{0x4E},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x04},std::byte{0xC0},std::byte{0x00},std::byte{0x00},std::byte{0x1B},std::byte{0x00},std::byte{0x00}}};
inline constexpr std::array<std::byte, 11> unseeded_fov{{std::byte{0x4E},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0xC0},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00}}};
}
