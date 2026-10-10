#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../middleware/content/packages/tables/roster_intersection.h"
#include "../../../state/build_data/scenarios/definition.h"

namespace dawn::client::content::scenarios {

namespace layouts = state::build_data::scenarios;
namespace reader = middleware::content::packages::reader;

// the walk reaches 5,826 distinct objects; headroom keeps the open-addressed probe cheap
inline constexpr std::size_t kObjectMemoCapacity = 16'384;
inline constexpr std::uint16_t kNotARosterGroup = 0xFFFF;

struct ObjectMemo {
    std::uint32_t tag{};
    std::uint16_t group{kNotARosterGroup};
};

struct SlotRecord {
    std::uint16_t index{};
    std::uint8_t type{};
    std::uint8_t flags{};
};

struct RosterStorage {
    std::vector<std::byte> scenario;
    std::vector<std::byte> entry;
    std::vector<std::byte> registry;
    std::vector<std::byte> object;
    std::vector<std::byte> chain;
    std::array<ObjectMemo, kObjectMemoCapacity> memo{};
    std::array<layouts::RosterGroup, layouts::kRosterGroupCapacity> groups{};
    std::size_t groupCount{};
    std::array<SlotRecord, layouts::kRosterSlotCapacity> slots{};
    std::size_t slotCount{};
    bool slotsOverflowed{};
    std::size_t unresolvedGroups{};
    std::size_t cursor{};
    std::size_t reads{};
};

inline constexpr std::size_t kRosterReadBudget = 150;

inline constexpr std::size_t kLiveTagCapacity = 1'024;
// packages register during boot, so an early attempt reads fewer; one run latched at 417 of
// 466 and the dropped destination was the Tower
inline constexpr std::uint64_t kResolveWindowMs = 60'000;
// compact_rows is destructive, so a round with unread rows must not count as finished; a
// partial domain leaves a destination undescribed and the client on a black screen at load-in
inline constexpr std::uint32_t kMaxResolveRounds = 8;
inline constexpr std::size_t kResolveReadBudget = 150;

struct LiveTag {
    std::uint32_t tag{};
    std::array<char, layouts::kSpawnStemCapacity> stem{};
    std::uint8_t stemLength{};
};

struct Storage {
    std::array<LiveTag, kLiveTagCapacity> liveTags{};
    std::size_t liveTagCount{};
    std::array<layouts::Definition, layouts::kDefinitionCapacity> rows{};
    std::size_t rowCount{};
    std::array<std::uint32_t, layouts::kDefinitionCapacity> rowPatch{};
    std::array<std::uint8_t, layouts::kDefinitionCapacity> resolved{};
    std::size_t resolvedCount{};
    std::size_t liveRowCount{};
    std::size_t resolveCursor{};
    std::uint64_t resolveDeadlineTick{};
    std::vector<std::byte> blob{};
    RosterStorage roster{};
    std::size_t keptCount{};
    std::uint32_t resolveRounds{};
    bool collected{};
    bool compacted{};
};

[[nodiscard]] bool
collect_rows(const reader::Source& source, Storage& storage, const char*& reason) noexcept;

[[nodiscard]] bool
resolve_pending(const reader::Source& source, reader::Scratch& scratch, Storage& storage) noexcept;

void compact_rows(Storage& storage) noexcept;

void rearm_resolve(Storage& storage) noexcept;

struct Candidate {
    std::uint16_t group{};
    std::uint32_t key{};
    bool bindsPlayer{};
    bool reportsLifetime{};
    bool primaryRegistry{};
};

[[nodiscard]] constexpr bool same_group_layout(const layouts::RosterGroup& left,
                                               const layouts::RosterGroup& right) noexcept {
    if (left.registryKey != right.registryKey || left.slotCount != right.slotCount) {
        return false;
    }
    for (std::size_t slot = 0; slot < left.slotCount; ++slot) {
        if (left.slotTypes[slot] != right.slotTypes[slot]
            || left.slotFlags[slot] != right.slotFlags[slot]
            || left.slotIndices[slot] != right.slotIndices[slot]) {
            return false;
        }
    }
    return true;
}

struct Walk {
    middleware::content::packages::tables::RosterIntersection intersection{};
    std::array<Candidate, middleware::content::packages::tables::kRosterKeyCapacity> candidates{};
    std::size_t candidateCount{};
};

void publish_groups(Walk& walk, layouts::Definition& row) noexcept;

[[nodiscard]] bool resolve_object(const reader::Source& source,
                                  reader::Scratch& scratch,
                                  RosterStorage& storage,
                                  std::uint32_t objectTag,
                                  std::uint16_t& group) noexcept;

[[nodiscard]] bool build_rosters(const reader::Source& source,
                                 reader::Scratch& scratch,
                                 RosterStorage& storage,
                                 std::span<layouts::Definition> rows) noexcept;

} // namespace dawn::client::content::scenarios
