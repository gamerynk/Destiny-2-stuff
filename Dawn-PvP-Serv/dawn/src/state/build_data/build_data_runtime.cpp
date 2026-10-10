#include <Windows.h>

#include <cstdint>
#include <span>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "../content/content_catalog.h"
#include "abilities/ability_bucket_catalog.h"
#include "cache/internal.h"
#include "collectibles/collectible_catalog.h"
#include "constants/investment_constant_catalog.h"
#include "hash_names/hash_name_catalog.h"
#include "inventory/buckets/inventory_bucket_catalog.h"
#include "items/details/item_detail_catalog.h"
#include "items/socket_plugs/socket_plug_catalog.h"
#include "material_requirements/material_requirement_catalog.h"
#include "progressions/progression_catalog.h"
#include "runtime.h"
#include "runtime/build_data_catalog_runtime.h"
#include "runtime/domain_markers.h"
#include "runtime/persistence/build_data_persistence.h"
#include "scenarios/scenario_catalog.h"
#include "socket_entry_lists/socket_entry_list_catalog.h"
#include "spawn_sets/spawn_set_catalog.h"
#include "vendors/vendor_catalog.h"

namespace dawn::state::build_data {
namespace {

constexpr std::wstring_view kCacheDirectorySuffix = L"\\cache";
constexpr std::wstring_view kCacheFileSuffix = L"\\cache\\build_data.bin";

} // namespace

bool initialize(void* module, std::uint64_t configuredEquipmentHash) noexcept {
    runtime::persistence::Context& persistenceState = runtime::persistence::context();
    AcquireSRWLockExclusive(&persistenceState.lock);
    runtime::persistence::clear_locked(persistenceState);
    runtime::clear_catalogs();
    if (module == nullptr) {
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return true;
    }

    core::path::Buffer moduleDirectory;
    if (!core::path::artifact_directory(module, moduleDirectory)) {
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return false;
    }
    persistenceState.cacheDirectory = moduleDirectory;
    persistenceState.cachePath = moduleDirectory;
    if (!core::path::append(persistenceState.cacheDirectory, kCacheDirectorySuffix)
        || !core::path::append(persistenceState.cachePath, kCacheFileSuffix)
        || !cache::current_build_identity(configuredEquipmentHash,
                                          persistenceState.buildIdentity)) {
        runtime::persistence::clear_locked(persistenceState);
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return false;
    }
    persistenceState.enabled = true;

    cache::records::DomainCounts counts{};
    const cache::LoadStatus status =
        cache::load(persistenceState.cachePath.chars.data(),
                    persistenceState.buildIdentity,
                    runtime::persistence::scratch_domains(persistenceState),
                    counts);
    if (status == cache::LoadStatus::missing) {
        runtime::persistence::release_scratch_locked(persistenceState);
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return true;
    }
    if (status == cache::LoadStatus::stale) {
        // a stale cache is replaced only after every domain is complete
        persistenceState.replaceStaleCache = true;
        runtime::persistence::release_scratch_locked(persistenceState);
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return true;
    }
    const cache::records::Domains domains =
        runtime::persistence::occupied_domains(persistenceState, counts);
    bool detailsReplaced = false;
    if (domains.itemDetails.empty()) {
        items::details::clear();
        detailsReplaced = true;
    } else {
        detailsReplaced = items::details::replace(domains.itemDetails);
    }
    const constants::InvestmentConstants cachedConstants{
        domains.constants.extracted != 0,
        domains.constants.lightStatRow,
        domains.constants.weaponPowerStatRow,
        domains.constants.characterStatRows,
    };
    if (status != cache::LoadStatus::loaded || !constants::replace(cachedConstants)
        || !content::replace(domains.named) || !content::seal() || !items::replace(domains.items)
        || !collectibles::replace(domains.collectibles)
        || !material_requirements::replace(domains.materialRequirementSets)
        || !inventory::buckets::replace(domains.inventoryBuckets)
        || !socket_entry_lists::replace(domains.socketEntryLists)
        // the per-entry tables feed subclass selection; without them a cache hit skips the package build and no ability is picked
        || !socket_entry_lists::replace_entry_tables(domains.socketEntryTables) || !detailsReplaced
        || !items::socket_plugs::replace(
            domains.socketPlugRules, domains.socketPlugPools, domains.socketPlugMembers)
        || !abilities::replace(domains.abilityBuckets)
        || !progressions::replace(domains.progressions)
        // activity message 1 reads the layouts; without them a cache hit skips the package build and
        // every destination falls back
        || !scenarios::replace(domains.scenarios, domains.rosterGroups)
        // an empty catalog is complete, so the replace is skipped rather than failed
        || (!domains.spawnStems.empty()
            && !spawn_sets::replace(
                domains.spawnStems, domains.spawnNameHashes, domains.spawnPoints))
        || (!domains.vendorIndex.empty()
            && !vendors::replace(domains.vendorIndex,
                                 domains.vendorDefinitions,
                                 domains.vendorSaleRows,
                                 domains.vendorInstalledRows))
        || !hash_names::replace(domains.hashNames)) {
        runtime::clear_catalogs();
        runtime::persistence::clear_locked(persistenceState);
        ReleaseSRWLockExclusive(&persistenceState.lock);
        return false;
    }
    runtime::details::publish();
    runtime::named::publish();
    runtime::ability_buckets::publish();
    runtime::spawn_catalog::publish();
    runtime::name_catalog::publish();
    persistenceState.persisted = true;
    runtime::persistence::release_scratch_locked(persistenceState);
    ReleaseSRWLockExclusive(&persistenceState.lock);
    return true;
}

void shutdown() noexcept {
    runtime::persistence::Context& persistenceState = runtime::persistence::context();
    AcquireSRWLockExclusive(&persistenceState.lock);
    runtime::clear_catalogs();
    runtime::persistence::clear_locked(persistenceState);
    ReleaseSRWLockExclusive(&persistenceState.lock);
}

} // namespace dawn::state::build_data
