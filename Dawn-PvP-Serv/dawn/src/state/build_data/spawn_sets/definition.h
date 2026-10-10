#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::spawn_sets {

inline constexpr std::size_t kPositionComponents = 3;
inline constexpr std::size_t kPointCapacity = 65'536;

inline constexpr std::size_t kStemCapacity = 128;
inline constexpr std::size_t kStemNameCapacity = 32;
inline constexpr std::size_t kNameHashCapacity = 4'096;
inline constexpr std::size_t kBubbleMaskBytes = 32;
inline constexpr std::size_t kPackageCapacity = 12;

struct Stem {
    std::array<char, kStemNameCapacity> name{};
    std::uint32_t pointCount{};
    std::uint16_t setCount{};
    std::uint16_t nameHashOffset{};
    std::uint16_t nameHashCount{};
    std::uint8_t nameLength{};
};

struct NameHash {
    std::uint32_t value{};
    std::uint32_t pointCount{};
    std::uint16_t stemIndex{};
    std::array<std::uint8_t, kBubbleMaskBytes> bubbleMask{};
    // every installed set is bound; an unbound set is offered for every bubble of the map rather than hidden
    std::uint8_t unbound{};
    std::uint8_t inMapPackage{};
    std::uint8_t activityPackageCount{};
    std::uint8_t activityPackageOverflow{};
    std::array<std::uint16_t, kPackageCapacity> activityPackages{};
};

struct Point {
    std::array<float, kPositionComponents> position{};
    std::uint32_t nameHash{};
    std::uint16_t stemIndex{};
};

} // namespace dawn::state::build_data::spawn_sets
