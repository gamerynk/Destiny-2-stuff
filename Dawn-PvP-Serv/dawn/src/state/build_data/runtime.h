#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "abilities/definition.h"
#include "collectibles/collectible_catalog.h"
#include "constants/definition.h"
#include "definition.h"
#include "hash_names/definition.h"
#include "inventory/buckets/definition.h"
#include "items/details/definition.h"
#include "items/item_catalog.h"
#include "items/socket_plugs/definition.h"
#include "material_requirements/material_requirement_catalog.h"
#include "progressions/definition.h"
#include "scenarios/definition.h"
#include "socket_entry_lists/definition.h"
#include "spawn_sets/definition.h"
#include "vendors/definition.h"

namespace dawn::state::build_data {

// once a snapshot is on disk, later replacements are refused until State restarts
// a failed first commit withdraws the publication and restores nothing
[[nodiscard]] bool initialize(void* module, std::uint64_t configuredEquipmentHash) noexcept;

void shutdown() noexcept;

[[nodiscard]] bool named_catalog_ready() noexcept;

[[nodiscard]] bool publish_named_catalog() noexcept;

[[nodiscard]] bool item_definitions_ready() noexcept;

[[nodiscard]] std::size_t item_definition_count() noexcept;

[[nodiscard]] bool
publish_item_definitions(std::span<const items::Definition> definitions) noexcept;

[[nodiscard]] bool find_item_definition(std::uint32_t definitionHash,
                                        std::uint8_t bucketId,
                                        items::Definition& definition) noexcept;

[[nodiscard]] bool find_item_definition_hash(std::uint32_t definitionHash,
                                             items::Definition& definition) noexcept;

[[nodiscard]] bool find_item_definition_index(std::uint16_t definitionIndex,
                                              items::Definition& definition) noexcept;

[[nodiscard]] bool collectible_definitions_ready() noexcept;

[[nodiscard]] bool
publish_collectible_definitions(std::span<const collectibles::Definition> definitions) noexcept;

[[nodiscard]] bool
find_collectible_item_definition_index(std::uint16_t collectibleIndex,
                                       std::uint16_t& itemDefinitionIndex) noexcept;

[[nodiscard]] bool find_collectible_definition(std::uint16_t collectibleIndex,
                                               collectibles::Definition& definition) noexcept;

[[nodiscard]] bool material_requirement_sets_ready() noexcept;

[[nodiscard]] bool publish_material_requirement_sets(
    std::span<const material_requirements::Definition> definitions) noexcept;

[[nodiscard]] bool
find_material_requirement_set(std::uint16_t requirementSetIndex,
                              material_requirements::Definition& definition) noexcept;

[[nodiscard]] bool configured_item_details_ready() noexcept;

[[nodiscard]] bool
publish_configured_item_details(std::span<const items::details::Definition> definitions) noexcept;

[[nodiscard]] bool find_configured_item_detail(std::uint16_t definitionIndex,
                                               items::details::Definition& definition) noexcept;

[[nodiscard]] bool socket_plug_rules_ready() noexcept;

[[nodiscard]] bool
publish_socket_plug_rules(std::span<const items::socket_plugs::Rule> rules,
                          std::span<const items::socket_plugs::Pool> pools,
                          std::span<const items::socket_plugs::Member> members) noexcept;

[[nodiscard]] bool is_socket_plug_allowed(std::uint16_t itemDefinitionIndex,
                                          std::uint8_t lane,
                                          std::uint16_t plugDefinitionIndex) noexcept;

[[nodiscard]] bool is_profile_action_source(std::uint16_t itemDefinitionIndex,
                                            std::uint8_t bucketId) noexcept;

[[nodiscard]] bool is_consumed_on_apply(std::uint16_t itemDefinitionIndex,
                                        std::uint8_t bucketId) noexcept;

[[nodiscard]] bool progression_definitions_ready() noexcept;

[[nodiscard]] bool
publish_progression_definitions(std::span<const progressions::Definition> definitions) noexcept;

[[nodiscard]] bool find_progression_slots(progressions::Scope scope,
                                          std::span<std::uint16_t> output,
                                          std::size_t& count) noexcept;

[[nodiscard]] bool ability_buckets_ready() noexcept;

[[nodiscard]] bool
publish_ability_buckets(std::span<const abilities::Definition> definitions) noexcept;

[[nodiscard]] bool find_ability_buckets(std::uint16_t socketEntryListIndex,
                                        const abilities::Selection& selection,
                                        abilities::Definition& definition) noexcept;

[[nodiscard]] bool investment_constants_ready() noexcept;

[[nodiscard]] bool
publish_investment_constants(const constants::InvestmentConstants& value) noexcept;

[[nodiscard]] bool find_investment_constants(constants::InvestmentConstants& value) noexcept;

[[nodiscard]] bool inventory_bucket_descriptors_ready() noexcept;

[[nodiscard]] bool publish_inventory_bucket_descriptors(
    std::span<const inventory::buckets::Descriptor> descriptors) noexcept;

[[nodiscard]] bool
find_inventory_bucket_descriptor(std::uint8_t bucketId,
                                 inventory::buckets::Descriptor& descriptor) noexcept;

[[nodiscard]] bool socket_entry_lists_ready() noexcept;

[[nodiscard]] std::size_t socket_entry_list_count() noexcept;

[[nodiscard]] bool
publish_socket_entry_lists(std::span<const socket_entry_lists::Definition> definitions,
                           std::span<const socket_entry_lists::EntryTable> entryTables) noexcept;

[[nodiscard]] bool find_socket_entry_table(std::uint16_t definitionIndex,
                                           socket_entry_lists::EntryTable& table) noexcept;

[[nodiscard]] bool find_socket_entry_list(std::uint16_t definitionIndex,
                                          socket_entry_lists::Definition& definition) noexcept;

[[nodiscard]] bool scenario_layouts_ready() noexcept;

[[nodiscard]] bool
publish_scenario_layouts(std::span<const scenarios::Definition> definitions,
                         std::span<const scenarios::RosterGroup> groups) noexcept;

[[nodiscard]] bool find_roster_group(std::size_t index, scenarios::RosterGroup& group) noexcept;

[[nodiscard]] bool find_roster_group_by_key(std::uint32_t key,
                                            std::uint32_t objectTag,
                                            scenarios::RosterGroup& group) noexcept;

[[nodiscard]] std::size_t scenario_layout_count() noexcept;

[[nodiscard]] bool snapshot_scenario_layouts(std::span<scenarios::Definition> output,
                                             std::size_t& count) noexcept;

[[nodiscard]] bool hash_names_ready() noexcept;

[[nodiscard]] bool publish_hash_names(std::span<const hash_names::Name> names) noexcept;

[[nodiscard]] bool find_hash_name(std::uint32_t hash, hash_names::Name& name) noexcept;

[[nodiscard]] bool spawn_sets_ready() noexcept;

[[nodiscard]] bool publish_spawn_sets(std::span<const spawn_sets::Stem> stems,
                                      std::span<const spawn_sets::NameHash> nameHashes,
                                      std::span<const spawn_sets::Point> points) noexcept;

[[nodiscard]] bool
find_nearest_spawn_point(std::string_view stem,
                         const std::array<float, spawn_sets::kPositionComponents>& position,
                         spawn_sets::Point& point,
                         float& distance) noexcept;

[[nodiscard]] bool snapshot_spawn_name_hashes(std::span<spawn_sets::NameHash> output,
                                              std::size_t& count) noexcept;

[[nodiscard]] bool find_spawn_sets(std::string_view stem,
                                   std::span<spawn_sets::NameHash> output,
                                   std::size_t& count) noexcept;

[[nodiscard]] bool find_scenario_layout(std::string_view name,
                                        scenarios::Definition& definition) noexcept;

[[nodiscard]] bool vendor_catalog_ready() noexcept;

[[nodiscard]] bool
publish_vendor_catalog(std::span<const vendors::IndexEntry> index,
                       std::span<const vendors::Definition> definitions,
                       std::span<const vendors::SaleRow> saleRows,
                       std::span<const vendors::InstalledRow> installedRows) noexcept;

[[nodiscard]] bool find_vendor_index(std::uint32_t definitionHash,
                                     vendors::IndexEntry& entry) noexcept;

[[nodiscard]] bool find_vendor_definition(std::uint32_t definitionHash,
                                          vendors::Definition& definition) noexcept;

[[nodiscard]] bool persist() noexcept;

} // namespace dawn::state::build_data
