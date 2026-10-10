#include "build_data_catalog_runtime.h"

#include "../../content/content_catalog.h"
#include "../abilities/ability_bucket_catalog.h"
#include "../collectibles/collectible_catalog.h"
#include "../constants/investment_constant_catalog.h"
#include "../hash_names/hash_name_catalog.h"
#include "../inventory/buckets/inventory_bucket_catalog.h"
#include "../items/details/item_detail_catalog.h"
#include "../items/socket_plugs/socket_plug_catalog.h"
#include "../material_requirements/material_requirement_catalog.h"
#include "../progressions/progression_catalog.h"
#include "../runtime.h"
#include "../scenarios/scenario_catalog.h"
#include "../socket_entry_lists/socket_entry_list_catalog.h"
#include "../spawn_sets/spawn_set_catalog.h"
#include "../vendors/vendor_catalog.h"
#include "domain_markers.h"
#include "persistence/publication_transaction.h"

namespace dawn::state::build_data {
namespace {

[[nodiscard]] bool
valid_detail_publication(std::span<const items::details::Definition> definitions) noexcept {
    const std::size_t itemCount = items::count();
    const std::size_t socketCount = socket_entry_lists::count();
    if (itemCount == 0 || socketCount == 0 || inventory::buckets::count() == 0
        || (!definitions.empty() && !items::details::valid(definitions))) {
        return false;
    }
    // plug rows share the domain with equipped rows and cannot be equipped, so equipment slot and
    // bucket routing are checked where a loadout uses them
    for (const items::details::Definition& definition : definitions) {
        items::Definition item{};
        if (definition.definitionIndex >= itemCount
            || definition.socketEntryListIndex >= socketCount
            || !items::find_index(definition.definitionIndex, item)
            || item.bucketId != definition.bucketId) {
            return false;
        }
        for (const std::uint16_t plugIndex : definition.initialPlugIndices) {
            if (plugIndex != items::details::kUnavailableItemIndex && plugIndex >= itemCount) {
                return false;
            }
        }
    }
    return true;
}

void rollback_ability_publication() noexcept {
    runtime::ability_buckets::clear();
    abilities::clear();
}

void rollback_name_catalog_publication() noexcept {
    runtime::name_catalog::clear();
    hash_names::clear();
}

void rollback_spawn_catalog_publication() noexcept {
    runtime::spawn_catalog::clear();
    spawn_sets::clear();
}

void rollback_detail_publication() noexcept {
    runtime::details::clear();
    items::details::clear();
}

} // namespace

bool configured_item_details_ready() noexcept {
    return runtime::details::ready();
}

bool publish_configured_item_details(
    std::span<const items::details::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    if (!transaction.active() || !valid_detail_publication(definitions)) {
        return false;
    }
    if (definitions.empty()) {
        items::details::clear();
    } else if (!items::details::replace(definitions)) {
        return false;
    }
    // row count does not matter; an empty loadout is a complete one
    runtime::details::publish();
    return transaction.finish(true, rollback_detail_publication);
}

bool find_configured_item_detail(std::uint16_t definitionIndex,
                                 items::details::Definition& definition) noexcept {
    definition = {};
    return configured_item_details_ready() && items::details::find(definitionIndex, definition);
}

bool progression_definitions_ready() noexcept {
    return progressions::count() != 0;
}

bool publish_progression_definitions(
    std::span<const progressions::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active()
           && transaction.finish(progressions::replace(definitions), progressions::clear);
}

bool scenario_layouts_ready() noexcept {
    return scenarios::count() != 0;
}

bool publish_scenario_layouts(std::span<const scenarios::Definition> definitions,
                              std::span<const scenarios::RosterGroup> groups) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active()
           && transaction.finish(scenarios::replace(definitions, groups), scenarios::clear);
}

std::size_t scenario_layout_count() noexcept {
    return scenarios::count();
}

bool snapshot_scenario_layouts(std::span<scenarios::Definition> output,
                               std::size_t& count) noexcept {
    count = 0;
    return scenario_layouts_ready() && scenarios::snapshot(output, count);
}

bool hash_names_ready() noexcept {
    return runtime::name_catalog::ready();
}

bool publish_hash_names(std::span<const hash_names::Name> names) noexcept {
    runtime::persistence::Transaction transaction;
    if (!transaction.active()) {
        return false;
    }
    if (!hash_names::replace(names)) {
        return transaction.finish(false, rollback_name_catalog_publication);
    }
    runtime::name_catalog::publish();
    return transaction.finish(true, rollback_name_catalog_publication);
}

bool find_hash_name(std::uint32_t hash, hash_names::Name& name) noexcept {
    name = {};
    return hash_names_ready() && hash_names::find(hash, name);
}

bool spawn_sets_ready() noexcept {
    return runtime::spawn_catalog::ready();
}

bool publish_spawn_sets(std::span<const spawn_sets::Stem> stems,
                        std::span<const spawn_sets::NameHash> nameHashes,
                        std::span<const spawn_sets::Point> points) noexcept {
    runtime::persistence::Transaction transaction;
    if (!transaction.active()) {
        return false;
    }
    // an empty catalog is complete; it is what a build with no installed spawn set means
    const bool replaced = stems.empty() ? nameHashes.empty() && points.empty()
                                        : spawn_sets::replace(stems, nameHashes, points);
    if (!replaced) {
        return transaction.finish(false, rollback_spawn_catalog_publication);
    }
    runtime::spawn_catalog::publish();
    return transaction.finish(true, rollback_spawn_catalog_publication);
}

bool snapshot_spawn_name_hashes(std::span<spawn_sets::NameHash> output,
                                std::size_t& count) noexcept {
    count = 0;
    return spawn_sets_ready() && spawn_sets::snapshot_hashes(output, count);
}

bool find_spawn_sets(std::string_view stem,
                     std::span<spawn_sets::NameHash> output,
                     std::size_t& count) noexcept {
    count = 0;
    spawn_sets::Stem row{};
    return spawn_sets_ready() && spawn_sets::find(stem, row)
           && spawn_sets::stem_hashes(row, output, count);
}

bool find_nearest_spawn_point(std::string_view stem,
                              const std::array<float, spawn_sets::kPositionComponents>& position,
                              spawn_sets::Point& point,
                              float& distance) noexcept {
    point = {};
    distance = 0.0F;
    return spawn_sets_ready() && spawn_sets::nearest_point(stem, position, point, distance);
}

bool find_scenario_layout(std::string_view name, scenarios::Definition& definition) noexcept {
    definition = {};
    return scenario_layouts_ready() && scenarios::find(name, definition);
}

bool find_progression_slots(progressions::Scope scope,
                            std::span<std::uint16_t> output,
                            std::size_t& count) noexcept {
    count = 0;
    return progression_definitions_ready() && progressions::slots(scope, output, count);
}

bool ability_buckets_ready() noexcept {
    return runtime::ability_buckets::ready();
}

bool publish_ability_buckets(std::span<const abilities::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    if (!transaction.active() || !abilities::replace(definitions)) {
        return false;
    }
    // row count does not matter; a loadout with no subclass is a complete one
    runtime::ability_buckets::publish();
    return transaction.finish(true, rollback_ability_publication);
}

bool find_ability_buckets(std::uint16_t socketEntryListIndex,
                          const abilities::Selection& selection,
                          abilities::Definition& definition) noexcept {
    definition = {};
    return ability_buckets_ready() && abilities::find(socketEntryListIndex, selection, definition);
}

bool investment_constants_ready() noexcept {
    constants::InvestmentConstants published{};
    return constants::find(published);
}

bool publish_investment_constants(const constants::InvestmentConstants& value) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && transaction.finish(constants::replace(value), constants::clear);
}

bool find_investment_constants(constants::InvestmentConstants& value) noexcept {
    return constants::find(value);
}

bool vendor_catalog_ready() noexcept {
    return vendors::count() != 0;
}

bool publish_vendor_catalog(std::span<const vendors::IndexEntry> index,
                            std::span<const vendors::Definition> definitions,
                            std::span<const vendors::SaleRow> saleRows,
                            std::span<const vendors::InstalledRow> installedRows) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active()
           && transaction.finish(vendors::replace(index, definitions, saleRows, installedRows),
                                 vendors::clear);
}

bool find_vendor_index(std::uint32_t definitionHash, vendors::IndexEntry& entry) noexcept {
    entry = {};
    return vendor_catalog_ready() && vendors::find_hash(definitionHash, entry);
}

bool find_vendor_definition(std::uint32_t definitionHash,
                            vendors::Definition& definition) noexcept {
    definition = {};
    return vendor_catalog_ready() && vendors::find(definitionHash, definition);
}

namespace runtime {

void clear_catalogs() noexcept {
    content::clear();
    named::clear();
    items::clear();
    collectibles::clear();
    material_requirements::clear();
    items::details::clear();
    details::clear();
    items::socket_plugs::clear();
    inventory::buckets::clear();
    socket_entry_lists::clear();
    rollback_ability_publication();
    progressions::clear();
    scenarios::clear();
    rollback_spawn_catalog_publication();
    rollback_name_catalog_publication();
    vendors::clear();
    constants::clear();
}

} // namespace runtime

} // namespace dawn::state::build_data
