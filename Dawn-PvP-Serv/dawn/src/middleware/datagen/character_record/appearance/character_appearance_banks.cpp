#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/filesystem/path.h"
#include "../../../../core/logging/log.h"
#include "../../../../core/settings/rule_text.h"
#include "../../../../state/build_data/runtime.h"
#include "internal.h"

namespace dawn::middleware::datagen::character_record::appearance {
namespace {

// socket types in priority order. a loadout has more distinct plug hashes than the overflow
// bank holds, so this order decides which survive
constexpr std::uint16_t kOverflowPriority[]{
    176, // weapon intrinsic frame
    677, // armour intrinsic
    61,  // sparrow drive
    92,  // weapon trait
    62,  // sparrow perks
    65,  // weapon barrel
    67,  // weapon blade
    159, // weapon guard
    177, // weapon magazine
    179, // weapon scope
    26,  // armour shader
    60,  // sparrow shader
    180, // weapon shader
};

// types outside the priority table rank behind every listed one
constexpr std::size_t kUnrankedPriority = std::size(kOverflowPriority);
constexpr std::size_t kRankedCapacity =
    family4::loadout::kItemCapacity * details::kInitialPlugCapacity;

struct Ranked {
    std::size_t priority{kUnrankedPriority};
    std::uint16_t definitionIndex{};
    std::uint16_t lane{};
    std::uint32_t definitionHash{};
};

[[nodiscard]] std::size_t priority_of(std::uint16_t socketType) noexcept {
    for (std::size_t rank = 0; rank < kUnrankedPriority; ++rank) {
        if (kOverflowPriority[rank] == socketType) {
            return rank;
        }
    }
    return kUnrankedPriority;
}

[[nodiscard]] bool ranked_less(const Ranked& left, const Ranked& right) noexcept {
    if (left.priority != right.priority) {
        return left.priority < right.priority;
    }
    if (left.definitionIndex != right.definitionIndex) {
        return left.definitionIndex < right.definitionIndex;
    }
    return left.lane < right.lane;
}

void append_perks(std::uint16_t definitionIndex,
                  std::span<std::uint16_t> bank,
                  std::size_t& count) noexcept {
    details::Definition detail{};
    if (definitionIndex == details::kUnavailableItemIndex
        || !state::build_data::find_configured_item_detail(definitionIndex, detail)) {
        return;
    }
    const std::size_t perks = detail.sandboxPerkCount < detail.sandboxPerks.size()
                                  ? detail.sandboxPerkCount
                                  : detail.sandboxPerks.size();
    for (std::size_t entry = 0; entry < perks && count < bank.size(); ++entry) {
        bank[count++] = detail.sandboxPerks[entry];
    }
}

void append_item_perks(const Equipped& equipped,
                       std::span<std::uint16_t> bank,
                       std::size_t& count) noexcept {
    append_perks(equipped.definitionIndex, bank, count);
    for (std::size_t lane = 0; lane < equipped.laneCount; ++lane) {
        append_perks(equipped.plugs[lane], bank, count);
    }
}

[[nodiscard]] bool weapon_slot(std::uint8_t slot) noexcept {
    return slot >= kFirstWeaponSlot
           && slot < kFirstWeaponSlot + static_cast<std::int8_t>(kWeaponSlotCount);
}

struct PerkOverride {
    std::uint32_t itemHash{};
    std::uint16_t from{};
    std::int32_t to{};
};

constexpr std::size_t kPerkOverrideCapacity = 128;

struct PerkOverrides {
    bool loaded{};
    std::size_t count{};
    std::array<PerkOverride, kPerkOverrideCapacity> rows{};
};

SRWLOCK g_overrideLock{SRWLOCK_INIT};
bool g_overridesAttempted{};
PerkOverrides g_overrides{};

// reads Restoration\weapon_perk_overrides.txt once: <hex item hash> <from perk> <to perk or -1>
[[nodiscard]] const PerkOverrides& perk_overrides() noexcept {
    AcquireSRWLockShared(&g_overrideLock);
    const bool attempted = g_overridesAttempted;
    ReleaseSRWLockShared(&g_overrideLock);
    if (attempted) {
        return g_overrides;
    }
    AcquireSRWLockExclusive(&g_overrideLock);
    if (!g_overridesAttempted) {
        std::array<char, 16384> text{};
        g_overrides = {};
        if (core::path::read_artifact_text(L"weapon_perk_overrides.txt", text)) {
            core::rule_text::Cursor cursor{text.data()};
            while (cursor.seek_field() && g_overrides.count < g_overrides.rows.size()) {
                const std::uint32_t item = cursor.read_hex();
                const std::int32_t from = cursor.read_decimal();
                const std::int32_t to = cursor.read_decimal();
                if (from < 0 || from > 0xFFFE || to > 0xFFFE) {
                    continue;
                }
                g_overrides.rows[g_overrides.count++] = {item, static_cast<std::uint16_t>(from), to};
            }
            g_overrides.loaded = g_overrides.count != 0;
        }
        std::array<char, 96> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=appearance stage=weapon_perk_overrides rows=%zu",
                                          g_overrides.count);
        if (written > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
        g_overridesAttempted = true;
    }
    ReleaseSRWLockExclusive(&g_overrideLock);
    return g_overrides;
}

void apply_weapon_overrides(std::uint16_t definitionIndex,
                            std::span<std::uint16_t> bank,
                            std::size_t& count) noexcept {
    const PerkOverrides& overrides = perk_overrides();
    state::build_data::items::Definition item{};
    if (!overrides.loaded || !state::build_data::find_item_definition_index(definitionIndex, item)) {
        return;
    }
    for (std::size_t row = 0; row < overrides.count; ++row) {
        const PerkOverride& rule = overrides.rows[row];
        if (rule.itemHash != item.definitionHash) {
            continue;
        }
        for (std::size_t entry = 0; entry < count; ++entry) {
            if (bank[entry] != rule.from) {
                continue;
            }
            if (rule.to >= 0) {
                bank[entry] = static_cast<std::uint16_t>(rule.to);
            } else {
                for (std::size_t next = entry; next + 1 < count; ++next) {
                    bank[next] = bank[next + 1];
                }
                bank[--count] = 0xFFFF;
                --entry;
            }
        }
    }
}

struct NodePerk {
    std::uint32_t subclassHash{};
    std::uint8_t node{};
    std::uint16_t perk{};
};

constexpr std::size_t kNodePerkCapacity = 512;
constexpr NodePerk kInstalledSubclassPerks[]{
#include "pc86657_subclass_perks.inc"
};
static_assert(std::size(kInstalledSubclassPerks) <= kNodePerkCapacity);
// shared marker node carries the subclass's always-on perks
constexpr std::uint8_t kSharedNode = 19;

struct NodePerks {
    bool loaded{};
    std::size_t count{};
    std::array<NodePerk, kNodePerkCapacity> rows{};
};

SRWLOCK g_nodePerkLock{SRWLOCK_INIT};
bool g_nodePerksAttempted{};
NodePerks g_nodePerks{};

// reads Restoration\subclass_perks.txt once: <hex subclass hash> <node> <perk index>
[[nodiscard]] const NodePerks& node_perks() noexcept {
    AcquireSRWLockShared(&g_nodePerkLock);
    const bool attempted = g_nodePerksAttempted;
    ReleaseSRWLockShared(&g_nodePerkLock);
    if (attempted) {
        return g_nodePerks;
    }
    AcquireSRWLockExclusive(&g_nodePerkLock);
    if (!g_nodePerksAttempted) {
        static std::array<char, core::rule_text::kRuleTextCapacity> text{};
        g_nodePerks = {};
        if (core::path::read_artifact_text(L"subclass_perks.txt", text)) {
            core::rule_text::Cursor cursor{text.data()};
            while (cursor.seek_field() && g_nodePerks.count < g_nodePerks.rows.size()) {
                const std::uint32_t subclass = cursor.read_hex();
                const std::int32_t node = cursor.read_decimal();
                const std::int32_t perk = cursor.read_decimal();
                if (node < 0 || node > 255 || perk < 0 || perk > 0xFFFE) {
                    continue;
                }
                g_nodePerks.rows[g_nodePerks.count++] = {
                    subclass, static_cast<std::uint8_t>(node), static_cast<std::uint16_t>(perk)};
            }
            g_nodePerks.loaded = g_nodePerks.count != 0;
        }
        const bool builtin = !g_nodePerks.loaded;
        if (builtin) {
            std::copy(std::begin(kInstalledSubclassPerks), std::end(kInstalledSubclassPerks),
                      g_nodePerks.rows.begin());
            g_nodePerks.count = std::size(kInstalledSubclassPerks);
            g_nodePerks.loaded = true;
        }
        std::array<char, 96> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=appearance stage=subclass_perks result=%s rows=%zu",
                                          builtin ? "builtin_pc86657" : "override",
                                          g_nodePerks.count);
        if (written > 0) {
            core::log::write(core::log::Channel::server,
                             g_nodePerks.loaded ? core::log::Level::info : core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(written)});
        }
        g_nodePerksAttempted = true;
    }
    ReleaseSRWLockExclusive(&g_nodePerkLock);
    return g_nodePerks;
}

[[nodiscard]] bool active_node(const state::CharacterState& character, std::uint8_t node) noexcept {
    if (node == character.classAbilityEntry || node == character.movementAbilityEntry
        || node == character.grenadeAbilityEntry || node == character.superAbilityEntry
        || node == character.meleeAbilityEntry || node == kSharedNode) {
        return true;
    }
    // a tree's perk nodes follow its melee: 11 owns 12-14, 15 owns 16-18, 21 owns 22-23
    switch (character.meleeAbilityEntry) {
    case 11:
        return node >= 12 && node <= 14;
    case 15:
        return node >= 16 && node <= 18;
    case 21:
        // node 20 is the bottom tree's super. Arcstrider and Sentinel save that tree with super 10,
        // so match by tree instead of the saved super entry
        return node == 20 || node == 22 || node == 23;
    default:
        return false;
    }
}

void append_subclass_perks(const state::CharacterState& character,
                           const family4::loadout::ResolvedInstances& instances,
                           std::span<std::uint16_t> bank,
                           std::size_t& count) noexcept {
    const NodePerks& table = node_perks();
    if (!table.loaded) {
        return;
    }
    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        if (instances.items[index].equipmentSlot != kSubclassEquipmentSlot) {
            continue;
        }
        state::build_data::items::Definition item{};
        if (!state::build_data::find_item_definition_index(
                instances.items[index].instance.baseDefinitionIndex, item)) {
            return;
        }
        for (std::size_t row = 0; row < table.count && count < bank.size(); ++row) {
            const NodePerk& perk = table.rows[row];
            if (perk.subclassHash != item.definitionHash || !active_node(character, perk.node)) {
                continue;
            }
            const auto end = bank.begin() + static_cast<std::ptrdiff_t>(count);
            if (std::find(bank.begin(), end, perk.perk) == end) {
                bank[count++] = perk.perk;
            }
        }
        return;
    }
}

} // namespace

void apply_overflow_hashes(const family4::loadout::ResolvedInstances& instances,
                           layout::Appearance& appearance) noexcept {
    std::array<Ranked, kRankedCapacity> ranked{};
    std::size_t rankedCount = 0;
    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        details::Definition detail{};
        Equipped equipped{};
        if (!resolve_equipped(instances.items[index], detail, equipped)) {
            continue;
        }
        for (std::size_t lane = 0; lane < equipped.laneCount && rankedCount < ranked.size();
             ++lane) {
            details::Definition plug{};
            if (equipped.plugs[lane] == details::kUnavailableItemIndex
                || !state::build_data::find_configured_item_detail(equipped.plugs[lane], plug)) {
                continue;
            }
            ranked[rankedCount++] = {priority_of(detail.socketTypes[lane]),
                                     equipped.definitionIndex,
                                     static_cast<std::uint16_t>(lane),
                                     plug.definitionHash};
        }
    }
    std::sort(
        ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(rankedCount), ranked_less);
    std::array<std::uint32_t, kRankedCapacity> unique{};
    std::size_t uniqueCount = 0;
    for (std::size_t entry = 0; entry < rankedCount; ++entry) {
        const std::uint32_t hash = ranked[entry].definitionHash;
        const auto end = unique.begin() + static_cast<std::ptrdiff_t>(uniqueCount);
        if (std::find(unique.begin(), end, hash) == end) {
            unique[uniqueCount++] = hash;
        }
    }
    // only slots the subclass left at the sentinel are free, so the two fills never collide
    std::size_t taken = 0;
    for (std::uint32_t& slot : appearance.overflowHashes) {
        if (taken >= uniqueCount) {
            return;
        }
        if (slot == layout::kNoHash) {
            slot = unique[taken++];
        }
    }
}

void apply_perk_banks(const state::CharacterState& character,
                      const family4::loadout::ResolvedInstances& instances,
                      layout::Appearance& appearance) noexcept {
    const std::array<std::span<std::uint16_t>, kWeaponSlotCount> weaponBanks{
        appearance.smallBankA, appearance.smallBankB, appearance.smallBankC};
    std::size_t characterCount = 0;
    // slot order is the character bank's own order, not the loadout's instance order
    for (std::uint8_t slot = 0; slot < layout::kRenderSlotCapacity; ++slot) {
        for (std::size_t index = 0; index < instances.itemCount; ++index) {
            if (instances.items[index].equipmentSlot != slot) {
                continue;
            }
            details::Definition detail{};
            Equipped equipped{};
            if (!resolve_equipped(instances.items[index], detail, equipped)) {
                continue;
            }
            if (weapon_slot(slot)) {
                std::size_t weaponCount = 0;
                const auto bank = weaponBanks[static_cast<std::size_t>(slot - kFirstWeaponSlot)];
                append_item_perks(equipped, bank, weaponCount);
                apply_weapon_overrides(equipped.definitionIndex, bank, weaponCount);
                continue;
            }
            append_item_perks(equipped, appearance.indexBank, characterCount);
        }
    }
    // a subclass has no socket block; its ability perks (Marksman's Dodge reload, Gambler's Dodge
    // melee recharge, tree perks) come from its talent grid, generated from the manifest
    append_subclass_perks(character, instances, appearance.indexBank, characterCount);
}

} // namespace dawn::middleware::datagen::character_record::appearance
