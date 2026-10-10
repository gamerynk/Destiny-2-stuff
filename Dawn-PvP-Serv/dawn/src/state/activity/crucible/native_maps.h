#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::state::activity::crucible {

inline constexpr std::string_view kSourceExecutableSha256 = "81964380664e7fcee3c620085a157fdeaf91fefacf7214907820f188bbeb4ced";
inline constexpr std::size_t kNativeMapCount = 31;
inline constexpr std::size_t kNativeCombatSpawnCount = 2166;
inline constexpr std::size_t kNativeTeamSpawnCount = 1254;
inline constexpr std::size_t kNativeArrivalPointCount = 31;

struct NativeMap final {
    std::string_view scenarioName;
    std::string_view displayName;
    std::uint32_t scenarioTag;
    std::uint32_t nameHash;
    std::uint32_t stringBankTag;
    std::uint16_t scenarioPackageId;
    std::size_t firstCombatSpawn;
    std::size_t combatSpawnCount;
    std::size_t firstArrivalPoint;
    std::size_t arrivalPointCount;
    std::size_t firstTeamSpawn;
    std::size_t teamSpawnCount;
};

struct CombatSpawn final {
    std::array<float, 3> position;
    std::array<float, 4> rotation;
    std::string_view authoredName;
    std::uint32_t nameHash;
    std::uint32_t placedTag;
    std::uint32_t objectTag;
    std::uint32_t registryTag;
    std::uint32_t registryKey;
    std::uint16_t slotIndex;
    std::int16_t bubbleIndex;
    std::int8_t team;
};

using TeamSpawn = CombatSpawn;

struct ArrivalPoint final {
    std::array<float, 3> position;
    std::array<float, 4> rotation;
    std::uint32_t nameHash;
    std::uint32_t setTag;
    std::uint16_t packageId;
};

[[nodiscard]] std::span<const NativeMap> native_maps() noexcept;
[[nodiscard]] const NativeMap* find_native_map(std::string_view scenarioName) noexcept;
[[nodiscard]] const NativeMap* find_native_map(std::uint32_t scenarioTag) noexcept;
[[nodiscard]] std::span<const CombatSpawn> combat_spawns(const NativeMap& map) noexcept;
[[nodiscard]] std::span<const TeamSpawn> team_spawns(const NativeMap& map) noexcept;
[[nodiscard]] std::span<const ArrivalPoint> arrival_points(const NativeMap& map) noexcept;

} // namespace dawn::state::activity::crucible
