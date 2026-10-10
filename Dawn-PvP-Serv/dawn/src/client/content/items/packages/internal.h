#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../../../core/filesystem/path.h"
#include "../../../../middleware/content/packages/reader/reader.h"
#include "../../../../middleware/content/packages/tables/definition_index_table.h"
#include "../../../../middleware/content/packages/tables/items.h"
#include "../../../../state/build_data/abilities/definition.h"
#include "../../../../state/build_data/collectibles/collectible_catalog.h"
#include "../../../../state/build_data/constants/definition.h"
#include "../../../../state/build_data/inventory/buckets/definition.h"
#include "../../../../state/build_data/items/details/definition.h"
#include "../../../../state/build_data/items/item_catalog.h"
#include "../../../../state/build_data/material_requirements/material_requirement_catalog.h"
#include "../../../../state/build_data/progressions/definition.h"
#include "../../../../state/build_data/runtime.h"

namespace dawn::client::content::items::packages {

namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

inline constexpr std::size_t kDetailCapacity =
    state::build_data::items::details::kDefinitionCapacity;

using DetailRequests = std::bitset<state::build_data::items::kDefinitionCapacity>;

struct AuthoredHashes {
    std::array<std::uint32_t, kDetailCapacity> values{};
    std::size_t count{};
};

struct DetailSource {
    const reader::Source* source{};
    reader::Scratch* scratch{};
    std::span<const std::byte> table{};
    tables::Array array{};
    std::vector<std::byte>* definition{};
};

// the container name is not always unique, so every match is a candidate
inline constexpr std::size_t kContainerCandidates = 16;

struct Storage {
    reader::Scratch scratch{};
    std::vector<std::byte> container{};
    std::vector<std::byte> child{};
    std::vector<std::byte> root{};
    std::vector<std::byte> definition{};
    std::vector<std::byte> plugSetTable{};
    std::array<std::uint8_t, state::build_data::items::kDefinitionCapacity> specialPlugCategories{};
    std::array<state::build_data::inventory::buckets::Descriptor,
               state::build_data::inventory::buckets::kDescriptorCapacity>
        bucketRows{};
    std::size_t bucketCount{};
    std::array<std::int8_t, state::build_data::inventory::buckets::kDescriptorCapacity>
        equipmentSlotByBucket{};
    DetailRequests detailRequests{};
    std::array<std::uint16_t, kDetailCapacity> requestedDetailIndices{};
    std::vector<state::build_data::items::details::Definition> details{};
    AuthoredHashes authoredHashes{};
    std::vector<std::byte> abilityTable{};
    std::vector<std::byte> abilityPool{};
    std::array<state::build_data::abilities::Definition,
               state::build_data::abilities::kDefinitionCapacity>
        abilityRows{};
    std::array<state::build_data::progressions::Definition,
               state::build_data::progressions::kDefinitionCapacity>
        progressionRows{};
    std::array<state::build_data::collectibles::Definition,
               state::build_data::collectibles::kDefinitionCapacity>
        collectibleRows{};
    std::array<state::build_data::material_requirements::Definition,
               state::build_data::material_requirements::kDefinitionCapacity>
        materialRequirementRows{};
    std::array<state::build_data::items::Definition, state::build_data::items::kDefinitionCapacity>
        rows{};
};

[[nodiscard]] bool collect_authored_hashes(AuthoredHashes& output) noexcept;

[[nodiscard]] bool authored(const AuthoredHashes& hashes, std::uint32_t hash) noexcept;

[[nodiscard]] bool equippable(const tables::items::Row& row) noexcept;

[[nodiscard]] bool publish_buckets(Storage& storage) noexcept;

void request(std::uint16_t definitionIndex, DetailRequests& requested) noexcept;

// a lane using native defaults falls back to this plug, so its detail must exist
void append_initial_plugs(const tables::items::Row& row,
                          std::uint64_t itemDefinitionCount,
                          DetailRequests& requested) noexcept;

[[nodiscard]] bool materialize_requests(const DetailRequests& requested,
                                        std::span<std::uint16_t> output,
                                        std::size_t& count) noexcept;

[[nodiscard]] bool build_detail(const DetailSource& source,
                                std::uint16_t definitionIndex,
                                state::build_data::items::details::Definition& detail,
                                tables::items::Row& item) noexcept;

[[nodiscard]] bool
read_investment_constants(const reader::Source& source,
                          reader::Scratch& scratch,
                          std::span<const std::byte> root,
                          std::vector<std::byte>& blob,
                          state::build_data::constants::InvestmentConstants& output) noexcept;

[[nodiscard]] bool build_ability_buckets(const reader::Source& source,
                                         reader::Scratch& scratch,
                                         std::span<const std::byte> listDefinition,
                                         std::vector<std::byte>& blob,
                                         const state::build_data::abilities::Selection& selection,
                                         state::build_data::abilities::Definition& output) noexcept;

[[nodiscard]] bool
build_character_abilities(const reader::Source& source,
                          reader::Scratch& scratch,
                          std::span<const std::byte> root,
                          std::vector<std::byte>& table,
                          std::vector<std::byte>& definition,
                          std::vector<std::byte>& blob,
                          std::span<state::build_data::abilities::Definition> output,
                          std::size_t& count) noexcept;

// inline rows, not index rows; the scope byte picks the replicated object holding the
// progression, and the row's place among rows of that scope is its slot there
[[nodiscard]] bool build_progressions(const reader::Source& source,
                                      reader::Scratch& scratch,
                                      std::span<const std::byte> root,
                                      std::vector<std::byte>& blob,
                                      std::span<state::build_data::progressions::Definition> output,
                                      std::size_t& count) noexcept;

[[nodiscard]] bool collect_keys(reader::BlockKeys& keys) noexcept;

[[nodiscard]] bool
investment_globals_tags(std::array<std::uint32_t, kContainerCandidates>& candidates,
                        std::size_t& count) noexcept;

[[nodiscard]] bool package_directory(core::path::Buffer& directory) noexcept;

void report_detail_failure(std::size_t slot, std::uint16_t definitionIndex) noexcept;

void report_ability_count(std::size_t count) noexcept;

void report_ability_failure(const char* stage,
                            std::size_t character,
                            std::size_t first,
                            std::size_t second) noexcept;

void report_detail_count(std::size_t requested, std::size_t built) noexcept;

void report_row_count(std::size_t walked,
                      std::size_t rows,
                      std::size_t skipped,
                      bool truncated) noexcept;

void report_socket_plug_count(std::size_t rules,
                              std::size_t pools,
                              std::size_t members,
                              std::size_t skipped) noexcept;

void report_bucket_equipment_mapping(std::size_t mappedSlots) noexcept;

void report_bucket_equipment_failure(const char* stage,
                                     std::size_t first,
                                     std::size_t second) noexcept;

void report(std::size_t published, const char* reason) noexcept;

[[nodiscard]] bool build_buckets(const reader::Source& source,
                                 Storage& storage,
                                 std::span<const std::byte> root) noexcept;

[[nodiscard]] bool build_socket_entry_lists(const reader::Source& source,
                                            Storage& storage,
                                            std::span<const std::byte> root) noexcept;

[[nodiscard]] bool build_collectibles(const reader::Source& source,
                                      Storage& storage,
                                      std::span<const std::byte> root,
                                      std::uint64_t itemDefinitionCount) noexcept;

[[nodiscard]] bool build_item_rows(const reader::Source& source,
                                   Storage& storage,
                                   const tables::Array& table,
                                   std::size_t& rowCount,
                                   const char*& reason) noexcept;

[[nodiscard]] bool build_material_requirements(const reader::Source& source,
                                               Storage& storage,
                                               std::span<const std::byte> root,
                                               std::uint64_t itemDefinitionCount) noexcept;

} // namespace dawn::client::content::items::packages
