#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/spawn_sets/definition.h"

namespace dawn::client::content::spawn_sets {

namespace spawn_state = state::build_data::spawn_sets;
namespace package_reader = middleware::content::packages::reader;

inline constexpr std::size_t kTagCapacity = 1'024;
inline constexpr std::size_t kComponentCapacity = 20'480;
inline constexpr std::size_t kContainerCapacity = 8'192;
inline constexpr std::size_t kSpawnComponentCapacity = 2'048;
inline constexpr std::size_t kAccumulatorCapacity = 8'192;
inline constexpr std::size_t kReadBudget = 512;
// block keys resolve during boot, so early reads fail on their own; after this the tag is
// dropped because the whole content pipeline waits behind this pass
inline constexpr std::uint64_t kReadWindowMs = 15'000;

struct Entry {
    std::uint32_t tag{};
    std::array<char, spawn_state::kStemNameCapacity> stem{};
    std::uint16_t stemIndex{};
    std::uint8_t stemLength{};
    std::uint16_t packageId{};
    std::uint8_t activityPackage{};
};

struct BubbleMask {
    std::array<std::uint8_t, spawn_state::kBubbleMaskBytes> bytes{};
};

struct SpawnComponent {
    std::uint32_t tag{};
    std::uint16_t entryIndex{};
};

struct HashSlot {
    std::uint32_t value{};
    std::uint32_t pointCount{};
    std::uint16_t stemIndex{};
    bool occupied{};
    BubbleMask mask{};
    std::uint8_t unbound{};
    std::uint8_t inMapPackage{};
    std::uint8_t activityPackageCount{};
    std::uint8_t activityPackageOverflow{};
    std::array<std::uint16_t, spawn_state::kPackageCapacity> activityPackages{};
};

struct Storage {
    std::array<Entry, kTagCapacity> entries{};
    std::size_t entryCount{};
    std::array<std::uint16_t, kTagCapacity> tagOrder{};
    std::array<BubbleMask, kTagCapacity> tagMasks{};
    std::array<std::uint8_t, kTagCapacity> tagBound{};
    std::array<std::uint32_t, kComponentCapacity> componentTags{};
    std::size_t componentCount{};
    std::array<std::uint32_t, kContainerCapacity> containerTags{};
    std::size_t containerCount{};
    std::array<SpawnComponent, kSpawnComponentCapacity> spawnComponents{};
    std::size_t spawnComponentCount{};
    std::size_t componentCursor{};
    std::size_t containerCursor{};
    std::array<spawn_state::Stem, spawn_state::kStemCapacity> stems{};
    std::size_t stemCount{};
    std::array<HashSlot, kAccumulatorCapacity> accumulator{};
    std::size_t accumulatedCount{};
    std::array<spawn_state::NameHash, spawn_state::kNameHashCapacity> nameHashes{};
    std::size_t nameHashCount{};
    std::array<spawn_state::Point, spawn_state::kPointCapacity> points{};
    std::size_t pointCount{};
    std::size_t pointsDropped{};
    std::size_t cursor{};
    std::size_t skipped{};
    std::uint64_t startedTick{};
    bool componentsOrdered{};
    bool collected{};
    bool finalized{};
    bool invalid{};
};

enum class AdvanceResult {
    pending,
    complete,
    invalid,
};

using BlobReader = bool (*)(void* context,
                            std::uint32_t tag,
                            std::span<const std::byte>& blob) noexcept;

// a blob waits out the disk before decoding, so fetching a run at once hides most of it; null means no read-ahead
using BlobPrefetch = void (*)(void* context, std::span<const std::uint32_t> tags) noexcept;

void reset(Storage& storage) noexcept;

[[nodiscard]] bool normalize_stem(std::wstring_view packageFamily,
                                  std::span<char> output,
                                  std::uint8_t& length) noexcept;

// a destination always loads its map packages, and an activity package only when its own slice-set entries name it
[[nodiscard]] bool activity_family(std::wstring_view packageFamily) noexcept;

[[nodiscard]] bool add_entry(Storage& storage, const package_reader::ClassEntry& entry) noexcept;

[[nodiscard]] bool add_component(Storage& storage, std::uint32_t tag) noexcept;

[[nodiscard]] bool add_container(Storage& storage, std::uint32_t tag) noexcept;

[[nodiscard]] bool finish_collection(Storage& storage) noexcept;

[[nodiscard]] AdvanceResult advance(Storage& storage,
                                    BlobReader reader,
                                    BlobPrefetch prefetch,
                                    void* context,
                                    std::size_t budget,
                                    bool skipUnreadable) noexcept;

} // namespace dawn::client::content::spawn_sets
