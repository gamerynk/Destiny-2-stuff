#include "build_data_persistence.h"

#include <Windows.h>

#include <algorithm>
#include <span>
#include <vector>

#include "../../abilities/ability_bucket_catalog.h"
#include "../../cache/internal.h"
#include "../../cache/records/validation.h"
#include "../../collectibles/collectible_catalog.h"
#include "../../constants/investment_constant_catalog.h"
#include "../../hash_names/hash_name_catalog.h"
#include "../../inventory/buckets/inventory_bucket_catalog.h"
#include "../../items/details/item_detail_catalog.h"
#include "../../items/socket_plugs/socket_plug_catalog.h"
#include "../../material_requirements/material_requirement_catalog.h"
#include "../../progressions/progression_catalog.h"
#include "../../runtime.h"
#include "../../scenarios/scenario_catalog.h"
#include "../../socket_entry_lists/socket_entry_list_catalog.h"
#include "../../spawn_sets/spawn_set_catalog.h"
#include "../../vendors/vendor_catalog.h"
#include "../build_data_catalog_runtime.h"
#include "../domain_markers.h"

namespace dawn::state::build_data::runtime::persistence {
namespace {

Context g_context;

template <typename Value, std::size_t Capacity>
[[nodiscard]] std::span<Value> ensure_scratch(std::vector<Value>& storage) noexcept {
    if (storage.size() != Capacity) {
        storage.assign(Capacity, Value{});
    }
    return storage;
}

[[nodiscard]] cache::records::InvestmentConstants
to_record(const constants::InvestmentConstants& value) noexcept {
    return {value.lightStatRow,
            value.weaponPowerStatRow,
            value.characterStatRows,
            value.extracted ? std::uint8_t{1} : std::uint8_t{0}};
}

[[nodiscard]] bool snapshot_domains_locked(Context& state,
                                           cache::records::DomainCounts& counts) noexcept {
    counts = {};
    const cache::records::MutableDomains scratch = scratch_domains(state);
    *scratch.constants = to_record(constants::snapshot());
    return content::snapshot(scratch.named, counts.named)
           && items::snapshot(scratch.items, counts.items)
           && collectibles::snapshot(scratch.collectibles, counts.collectibles)
           && material_requirements::snapshot(scratch.materialRequirementSets,
                                              counts.materialRequirementSets)
           && items::details::snapshot(scratch.itemDetails, counts.itemDetails)
           && items::socket_plugs::snapshot(scratch.socketPlugRules,
                                            counts.socketPlugRules,
                                            scratch.socketPlugPools,
                                            counts.socketPlugPools,
                                            scratch.socketPlugMembers,
                                            counts.socketPlugMembers)
           && inventory::buckets::snapshot(scratch.inventoryBuckets, counts.inventoryBuckets)
           && socket_entry_lists::snapshot(scratch.socketEntryLists, counts.socketEntryLists)
           && socket_entry_lists::snapshot_entry_tables(scratch.socketEntryTables,
                                                        counts.socketEntryTables)
           && abilities::snapshot(scratch.abilityBuckets, counts.abilityBuckets)
           && progressions::snapshot(scratch.progressions, counts.progressions)
           && scenarios::snapshot(scratch.scenarios, counts.scenarios)
           && scenarios::snapshot_groups(scratch.rosterGroups, counts.rosterGroups)
           && spawn_sets::snapshot(scratch.spawnStems, counts.spawnStems)
           && spawn_sets::snapshot_hashes(scratch.spawnNameHashes, counts.spawnNameHashes)
           && spawn_sets::snapshot_points(scratch.spawnPoints, counts.spawnPoints)
           && hash_names::snapshot(scratch.hashNames, counts.hashNames)
           && vendors::snapshot_index(scratch.vendorIndex, counts.vendorIndex)
           && vendors::snapshot_definitions(scratch.vendorDefinitions, counts.vendorDefinitions)
           && vendors::snapshot_sale_rows(scratch.vendorSaleRows, counts.vendorSaleRows)
           && vendors::snapshot_installed_rows(scratch.vendorInstalledRows,
                                               counts.vendorInstalledRows)
           && cache::records::canonicalize(scratch, counts);
}

} // namespace

Context& context() noexcept {
    return g_context;
}

bool all_domains_ready() noexcept {
    constants::InvestmentConstants published{};
    return runtime::named::ready() && item_definitions_ready() && configured_item_details_ready()
           && collectible_definitions_ready() && socket_plug_rules_ready()
           && material_requirement_sets_ready() && inventory_bucket_descriptors_ready()
           && socket_entry_lists_ready() && ability_buckets_ready()
           && progression_definitions_ready() && scenario_layouts_ready() && spawn_sets_ready()
           && hash_names_ready() && constants::find(published);
}

cache::records::MutableDomains scratch_domains(Context& state) noexcept {
    const auto named = ensure_scratch<content::Definition, content::kDefinitionCatalogCapacity>(
        state.namedScratch);
    const auto items =
        ensure_scratch<build_data::items::Definition, build_data::items::kDefinitionCapacity>(
            state.itemScratch);
    const auto collectibles =
        ensure_scratch<build_data::collectibles::Definition,
                       build_data::collectibles::kDefinitionCapacity>(state.collectibleScratch);
    const auto materialRequirementSets = ensure_scratch<material_requirements::Definition,
                                                        material_requirements::kDefinitionCapacity>(
        state.materialRequirementSetScratch);
    const auto itemDetails =
        ensure_scratch<build_data::items::details::Definition,
                       build_data::items::details::kDefinitionCapacity>(state.itemDetailScratch);
    const auto socketPlugRules =
        ensure_scratch<build_data::items::socket_plugs::Rule,
                       build_data::items::socket_plugs::kRuleCapacity>(state.socketPlugRuleScratch);
    const auto socketPlugPools =
        ensure_scratch<build_data::items::socket_plugs::Pool,
                       build_data::items::socket_plugs::kPoolCapacity>(state.socketPlugPoolScratch);
    const auto socketPlugMembers = ensure_scratch<build_data::items::socket_plugs::Member,
                                                  build_data::items::socket_plugs::kMemberCapacity>(
        state.socketPlugMemberScratch);
    const auto inventoryBuckets =
        ensure_scratch<inventory::buckets::Descriptor, inventory::buckets::kDescriptorCapacity>(
            state.inventoryBucketScratch);
    const auto socketEntryLists =
        ensure_scratch<socket_entry_lists::Definition, socket_entry_lists::kDefinitionCapacity>(
            state.socketEntryListScratch);
    const auto socketEntryTables =
        ensure_scratch<socket_entry_lists::EntryTable, socket_entry_lists::kEntryTableCapacity>(
            state.socketEntryTableScratch);
    const auto abilityBuckets =
        ensure_scratch<abilities::Definition, abilities::kDefinitionCapacity>(
            state.abilityBucketScratch);
    const auto progressions =
        ensure_scratch<progressions::Definition, progressions::kDefinitionCapacity>(
            state.progressionScratch);
    const auto scenarios = ensure_scratch<scenarios::Definition, scenarios::kDefinitionCapacity>(
        state.scenarioScratch);
    const auto rosterGroups =
        ensure_scratch<scenarios::RosterGroup, scenarios::kRosterGroupCapacity>(
            state.rosterGroupScratch);
    const auto spawnStems =
        ensure_scratch<spawn_sets::Stem, spawn_sets::kStemCapacity>(state.spawnStemScratch);
    const auto spawnNameHashes =
        ensure_scratch<spawn_sets::NameHash, spawn_sets::kNameHashCapacity>(
            state.spawnNameHashScratch);
    const auto spawnPoints =
        ensure_scratch<spawn_sets::Point, spawn_sets::kPointCapacity>(state.spawnPointScratch);
    const auto hashNames =
        ensure_scratch<hash_names::Name, hash_names::kNameCapacity>(state.hashNameScratch);
    const auto vendorIndex =
        ensure_scratch<vendors::IndexEntry, vendors::kIndexCapacity>(state.vendorIndexScratch);
    const auto vendorDefinitions =
        ensure_scratch<vendors::Definition, vendors::kDefinitionCapacity>(
            state.vendorDefinitionScratch);
    const auto vendorSaleRows =
        ensure_scratch<vendors::SaleRow, vendors::kSaleRowCapacity>(state.vendorSaleRowScratch);
    const auto vendorInstalledRows =
        ensure_scratch<vendors::InstalledRow, vendors::kInstalledRowCapacity>(
            state.vendorInstalledRowScratch);
    return {
        &state.constantsScratch,
        named,
        items,
        collectibles,
        materialRequirementSets,
        itemDetails,
        socketPlugRules,
        socketPlugPools,
        socketPlugMembers,
        inventoryBuckets,
        socketEntryLists,
        socketEntryTables,
        abilityBuckets,
        progressions,
        scenarios,
        rosterGroups,
        spawnStems,
        spawnNameHashes,
        spawnPoints,
        hashNames,
        vendorIndex,
        vendorDefinitions,
        vendorSaleRows,
        vendorInstalledRows,
    };
}

template <typename Value> void release_bank(std::vector<Value>& storage) noexcept {
    storage.clear();
    storage.shrink_to_fit();
}

void release_scratch_locked(Context& state) noexcept {
    release_bank(state.namedScratch);
    release_bank(state.itemScratch);
    release_bank(state.collectibleScratch);
    release_bank(state.materialRequirementSetScratch);
    release_bank(state.itemDetailScratch);
    release_bank(state.socketPlugRuleScratch);
    release_bank(state.socketPlugPoolScratch);
    release_bank(state.socketPlugMemberScratch);
    release_bank(state.inventoryBucketScratch);
    release_bank(state.socketEntryListScratch);
    release_bank(state.socketEntryTableScratch);
    release_bank(state.abilityBucketScratch);
    release_bank(state.progressionScratch);
    release_bank(state.scenarioScratch);
    release_bank(state.rosterGroupScratch);
    release_bank(state.spawnStemScratch);
    release_bank(state.spawnNameHashScratch);
    release_bank(state.spawnPointScratch);
    release_bank(state.hashNameScratch);
    release_bank(state.vendorIndexScratch);
    release_bank(state.vendorDefinitionScratch);
    release_bank(state.vendorSaleRowScratch);
    release_bank(state.vendorInstalledRowScratch);
    state.constantsScratch = {};
}

void clear_locked(Context& state) noexcept {
    release_scratch_locked(state);
    state.cacheDirectory = {};
    state.cachePath = {};
    state.buildIdentity = {};
    state.enabled = false;
    state.persisted = false;
    state.replaceStaleCache = false;
}

cache::records::Domains occupied_domains(Context& state,
                                         const cache::records::DomainCounts& counts) noexcept {
    const std::span<const items::details::Definition> itemDetails{state.itemDetailScratch.data(),
                                                                  counts.itemDetails};
    const std::span<const items::socket_plugs::Rule> socketPlugRules{
        state.socketPlugRuleScratch.data(), counts.socketPlugRules};
    const std::span<const items::socket_plugs::Pool> socketPlugPools{
        state.socketPlugPoolScratch.data(), counts.socketPlugPools};
    const std::span<const items::socket_plugs::Member> socketPlugMembers{
        state.socketPlugMemberScratch.data(), counts.socketPlugMembers};
    return {
        state.constantsScratch,
        std::span<const content::Definition>{state.namedScratch.data(), counts.named},
        std::span<const build_data::items::Definition>{state.itemScratch.data(), counts.items},
        std::span<const build_data::collectibles::Definition>{state.collectibleScratch.data(),
                                                              counts.collectibles},
        std::span<const material_requirements::Definition>{
            state.materialRequirementSetScratch.data(), counts.materialRequirementSets},
        itemDetails,
        socketPlugRules,
        socketPlugPools,
        socketPlugMembers,
        std::span<const inventory::buckets::Descriptor>{state.inventoryBucketScratch.data(),
                                                        counts.inventoryBuckets},
        std::span<const socket_entry_lists::Definition>{state.socketEntryListScratch.data(),
                                                        counts.socketEntryLists},
        std::span<const socket_entry_lists::EntryTable>{state.socketEntryTableScratch.data(),
                                                        counts.socketEntryTables},
        std::span<const abilities::Definition>{state.abilityBucketScratch.data(),
                                               counts.abilityBuckets},
        std::span<const progressions::Definition>{state.progressionScratch.data(),
                                                  counts.progressions},
        std::span<const scenarios::Definition>{state.scenarioScratch.data(), counts.scenarios},
        std::span<const scenarios::RosterGroup>{state.rosterGroupScratch.data(),
                                                counts.rosterGroups},
        std::span<const spawn_sets::Stem>{state.spawnStemScratch.data(), counts.spawnStems},
        std::span<const spawn_sets::NameHash>{state.spawnNameHashScratch.data(),
                                              counts.spawnNameHashes},
        std::span<const spawn_sets::Point>{state.spawnPointScratch.data(), counts.spawnPoints},
        std::span<const hash_names::Name>{state.hashNameScratch.data(), counts.hashNames},
        std::span<const vendors::IndexEntry>{state.vendorIndexScratch.data(), counts.vendorIndex},
        std::span<const vendors::Definition>{state.vendorDefinitionScratch.data(),
                                             counts.vendorDefinitions},
        std::span<const vendors::SaleRow>{state.vendorSaleRowScratch.data(), counts.vendorSaleRows},
        std::span<const vendors::InstalledRow>{state.vendorInstalledRowScratch.data(),
                                               counts.vendorInstalledRows},
    };
}

bool persist_if_complete_locked(Context& state) noexcept {
    if (!all_domains_ready() || state.persisted || !state.enabled) {
        return true;
    }
    cache::records::DomainCounts counts{};
    // one canonical snapshot keeps header counts and payload rows in the same commit
    if (!snapshot_domains_locked(state, counts)) {
        return false;
    }
    const bool written =
        cache::write(state.cacheDirectory.chars.data(),
                     state.cachePath.chars.data(),
                     state.buildIdentity,
                     occupied_domains(state, counts),
                     state.replaceStaleCache ? cache::WriteDisposition::replaceStale
                                             : cache::WriteDisposition::createOnly);
    if (!written) {
        return false;
    }
    state.persisted = true;
    state.replaceStaleCache = false;
    release_scratch_locked(state);
    return true;
}

} // namespace dawn::state::build_data::runtime::persistence

namespace dawn::state::build_data {

bool persist() noexcept {
    runtime::persistence::Context& state = runtime::persistence::context();
    AcquireSRWLockExclusive(&state.lock);
    const bool result = runtime::persistence::all_domains_ready()
                        && runtime::persistence::persist_if_complete_locked(state);
    ReleaseSRWLockExclusive(&state.lock);
    return result;
}

} // namespace dawn::state::build_data
