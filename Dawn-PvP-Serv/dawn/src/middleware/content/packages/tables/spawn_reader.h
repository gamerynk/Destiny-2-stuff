#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace dawn::middleware::content::packages::tables {

inline constexpr std::uint32_t kSpawnSetClass = 0x80809162U;
inline constexpr std::size_t kSpawnSetDescriptor = 8;
inline constexpr std::uint32_t kSpawnPointClass = 0x80809164U;
inline constexpr std::size_t kSpawnPointStride = 48;
inline constexpr std::size_t kSpawnPointRotationOffset = 0;
inline constexpr std::size_t kSpawnPointPositionOffset = 16;
inline constexpr std::size_t kSpawnPointNameHashOffset = 32;
inline constexpr std::size_t kRotationComponents = 4;
inline constexpr std::size_t kPositionComponents = 3;
// name hash of the set ordinary arrivals use
inline constexpr std::uint32_t kDefaultSpawnNameHash = 0x2EA8FB98U;
// hash an unnamed spawn point carries
inline constexpr std::uint32_t kUnnamedSpawnNameHash = 0x811C9DC5U;

struct SpawnPoint {
    std::array<float, kRotationComponents> rotation{};
    std::array<float, kPositionComponents> position{};
    std::uint32_t nameHash{};
};

[[nodiscard]] bool spawn_points(std::span<const std::byte> blob, Array& output) noexcept;

[[nodiscard]] bool spawn_point_at(std::span<const std::byte> blob,
                                  const Array& array,
                                  std::size_t index,
                                  SpawnPoint& output) noexcept;

} // namespace dawn::middleware::content::packages::tables
