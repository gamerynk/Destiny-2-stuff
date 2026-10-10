#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace dawn::middleware::content::packages::tables {

inline constexpr std::uint32_t kComponentClass = 0x808099D6U;
inline constexpr std::size_t kComponentResourceOffset = 216;
inline constexpr std::uint32_t kContainerClass = 0x80808A54U;
inline constexpr std::size_t kContainerBubbleMaskOffset = 8;
// 32 bytes, one bit per map-global bubble index
inline constexpr std::size_t kContainerBubbleMaskBytes = 32;
// no installed map declares an index above 169
inline constexpr std::size_t kBubbleIndexCapacity = kContainerBubbleMaskBytes * 8;
inline constexpr std::size_t kContainerMemberDescriptor = 40;
inline constexpr std::uint32_t kContainerMemberClass = 0x80808BB0U;
inline constexpr std::size_t kContainerMemberStride = 4;

[[nodiscard]] bool component_resource(std::span<const std::byte> blob, std::uint32_t& tag) noexcept;

// a set bit is a map-global bubble index the container belongs to
[[nodiscard]] bool container_bubble_mask(std::span<const std::byte> blob,
                                         std::span<std::uint8_t> output) noexcept;

[[nodiscard]] bool container_members(std::span<const std::byte> blob, Array& output) noexcept;

[[nodiscard]] bool container_member_at(std::span<const std::byte> blob,
                                       const Array& members,
                                       std::uint64_t index,
                                       std::uint32_t& tag) noexcept;

[[nodiscard]] bool bubble_in_mask(std::span<const std::uint8_t> mask, std::size_t index) noexcept;

} // namespace dawn::middleware::content::packages::tables
