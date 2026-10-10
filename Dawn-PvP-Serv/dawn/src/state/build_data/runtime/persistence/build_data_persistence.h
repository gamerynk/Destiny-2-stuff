#pragma once

#include <Windows.h>

#include <array>
#include <vector>

#include "../../../../core/filesystem/path.h"
#include "../../../content/content_catalog.h"
#include "../../abilities/definition.h"
#include "../../cache/records/domains.h"
#include "../../collectibles/collectible_catalog.h"
#include "../../constants/definition.h"
#include "../../definition.h"
#include "../../hash_names/definition.h"
#include "../../inventory/buckets/definition.h"
#include "../../items/details/definition.h"
#include "../../items/item_catalog.h"
#include "../../items/socket_plugs/definition.h"
#include "../../material_requirements/material_requirement_catalog.h"
#include "../../progressions/definition.h"
#include "../../scenarios/definition.h"
#include "../../socket_entry_lists/definition.h"
#include "../../spawn_sets/definition.h"
#include "../../vendors/definition.h"

namespace dawn::state::build_data::runtime::persistence {

struct Context {
    SRWLOCK lock{SRWLOCK_INIT};
    std::vector<content::Definition> namedScratch{};
    std::vector<items::Definition> itemScratch{};
    std::vector<collectibles::Definition> collectibleScratch{};
    std::vector<material_requirements::Definition> materialRequirementSetScratch{};
    std::vector<items::details::Definition> itemDetailScratch{};
    std::vector<items::socket_plugs::Rule> socketPlugRuleScratch{};
    std::vector<items::socket_plugs::Pool> socketPlugPoolScratch{};
    std::vector<items::socket_plugs::Member> socketPlugMemberScratch{};
    std::vector<inventory::buckets::Descriptor> inventoryBucketScratch{};
    std::vector<socket_entry_lists::Definition> socketEntryListScratch{};
    std::vector<socket_entry_lists::EntryTable> socketEntryTableScratch{};
    std::vector<abilities::Definition> abilityBucketScratch{};
    std::vector<progressions::Definition> progressionScratch{};
    std::vector<scenarios::Definition> scenarioScratch{};
    std::vector<scenarios::RosterGroup> rosterGroupScratch{};
    std::vector<spawn_sets::Stem> spawnStemScratch{};
    std::vector<spawn_sets::NameHash> spawnNameHashScratch{};
    std::vector<spawn_sets::Point> spawnPointScratch{};
    std::vector<hash_names::Name> hashNameScratch{};
    std::vector<vendors::IndexEntry> vendorIndexScratch{};
    std::vector<vendors::Definition> vendorDefinitionScratch{};
    std::vector<vendors::SaleRow> vendorSaleRowScratch{};
    std::vector<vendors::InstalledRow> vendorInstalledRowScratch{};
    cache::records::InvestmentConstants constantsScratch{};
    core::path::Buffer cacheDirectory;
    core::path::Buffer cachePath;
    BuildIdentity buildIdentity{};
    bool enabled{};
    bool persisted{};
    bool replaceStaleCache{};
};

[[nodiscard]] Context& context() noexcept;

[[nodiscard]] bool all_domains_ready() noexcept;

void clear_locked(Context& state) noexcept;

void release_scratch_locked(Context& state) noexcept;

[[nodiscard]] cache::records::MutableDomains scratch_domains(Context& state) noexcept;

[[nodiscard]] cache::records::Domains
occupied_domains(Context& state, const cache::records::DomainCounts& counts) noexcept;

[[nodiscard]] bool persist_if_complete_locked(Context& state) noexcept;

} // namespace dawn::state::build_data::runtime::persistence
