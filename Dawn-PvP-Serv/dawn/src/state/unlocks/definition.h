#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../build_data/progressions/definition.h"

namespace dawn::state::unlocks {

inline constexpr std::size_t kAccountFlagCapacity = 12'300;
inline constexpr std::size_t kProfileFlagCapacity = 512;
inline constexpr std::size_t kCharacterFlagCapacity = 256;
inline constexpr std::size_t kObjectiveValueCapacity = 6'200;
inline constexpr std::size_t kCharacterObjectFlagCapacity = 4'096;
inline constexpr std::size_t kCharacterObjectValueCapacity = 768;

inline constexpr std::size_t kProgressionLaneCount = 3;

// lane 0 is the progress the level walk reads
using ProgressionLanes = std::array<std::int32_t, kProgressionLaneCount>;

using ProgressionBank = std::array<ProgressionLanes, build_data::progressions::kDefinitionCapacity>;

// a set acquired flag is stored as its biased 2-bit true value
inline constexpr std::uint8_t kFlagSet = 2;
inline constexpr std::uint8_t kFlagClear = 0;

struct Table {
    std::array<std::uint8_t, kAccountFlagCapacity> accountFlags{};
    std::array<std::uint8_t, kProfileFlagCapacity> profileFlags{};
    std::array<std::uint8_t, kCharacterFlagCapacity> characterFlags{};
    std::array<std::int32_t, kObjectiveValueCapacity> objectiveValues{};
    std::array<std::uint8_t, kCharacterObjectFlagCapacity> characterObjectFlags{};
    std::array<std::int32_t, kCharacterObjectValueCapacity> characterObjectValues{};
    ProgressionBank accountProgressions{};
    ProgressionBank characterProgressions{};
};

} // namespace dawn::state::unlocks
