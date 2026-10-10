#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::abilities {

inline constexpr std::size_t kBucketCapacity = 12;
inline constexpr std::size_t kBucketHashCapacity = 16;
inline constexpr std::size_t kOverflowCapacity = 32;
// every combination is built (9 subclasses x 3 movement x 3 grenade x 3 trees x 2 class = 486)
inline constexpr std::size_t kDefinitionCapacity = 512;
inline constexpr std::uint8_t kEmptyBucketKind = 0xFF;

struct Bucket {
    std::uint8_t kind{kEmptyBucketKind};
    std::uint8_t hashCount{};
    std::array<std::uint32_t, kBucketHashCapacity> hashes{};
};

struct Selection {
    std::uint8_t movementEntry{};
    std::uint8_t grenadeEntry{};
    std::uint8_t superEntry{};
    std::uint8_t meleeEntry{};
    std::uint8_t classEntry{};

    friend bool operator==(const Selection&, const Selection&) = default;
};

struct Definition {
    std::uint16_t socketEntryListIndex{};
    Selection selection{};
    std::uint8_t overflowCount{};
    std::array<Bucket, kBucketCapacity> buckets{};
    std::array<std::uint32_t, kOverflowCapacity> overflow{};
};

} // namespace dawn::state::build_data::abilities
