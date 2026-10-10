#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#include "../../../../state/account/account_state.h"
#include "../../../../state/runtime/runtime.h"
#include "internal.h"

namespace dawn::client::content::items::packages {
namespace {

namespace domain = state::build_data::items::details;

[[nodiscard]] std::optional<std::int8_t> equipment_slot(std::uint8_t bucketId) noexcept {
    state::build_data::inventory::buckets::Descriptor descriptor{};
    if (state::build_data::find_inventory_bucket_descriptor(bucketId, descriptor)
        && descriptor.equipmentSlot
               != state::build_data::inventory::buckets::kUnavailableEquipmentSlot) {
        return descriptor.equipmentSlot;
    }
    return std::nullopt;
}

[[nodiscard]] domain::Definition to_detail(const tables::items::Row& row) noexcept {
    domain::Definition detail{};
    detail.definitionIndex = row.definitionIndex;
    detail.definitionHash = row.definitionHash;
    detail.bucketId = row.bucketId;
    detail.maxStackSize = row.maxStackSize;
    detail.instancedDefinitionState = row.instanced ? domain::InstancedDefinitionState::instanced
                                                    : domain::InstancedDefinitionState::stackable;
    detail.equipmentSlot =
        row.equipmentSlot.has_value() ? row.equipmentSlot : equipment_slot(row.bucketId);
    detail.ordinarySocketState =
        row.hasSockets ? domain::OrdinarySocketState::present : domain::OrdinarySocketState::absent;
    detail.ordinarySocketCount = row.socketCount;
    for (std::size_t lane = 0; lane < detail.initialPlugIndices.size(); ++lane) {
        detail.initialPlugIndices[lane] = row.initialPlugs[lane];
        detail.socketTypes[lane] = row.socketTypes[lane];
    }
    detail.socketEntryListIndex = row.socketEntryListIndex;
    const std::size_t stats =
        row.statCount < detail.stats.size() ? row.statCount : detail.stats.size();
    for (std::size_t entry = 0; entry < stats; ++entry) {
        detail.stats[entry] = {row.statRows[entry], row.statValues[entry]};
    }
    detail.statCount = static_cast<std::uint8_t>(stats);
    detail.gearArtIndex = row.gearArtIndex;
    for (std::size_t index = 0; index < detail.artArrangementIndices.size(); ++index) {
        detail.artArrangementIndices[index] = row.artArrangementIndices[index];
    }
    const std::size_t perks = row.sandboxPerkCount < detail.sandboxPerks.size()
                                  ? row.sandboxPerkCount
                                  : detail.sandboxPerks.size();
    for (std::size_t entry = 0; entry < perks; ++entry) {
        detail.sandboxPerks[entry] = row.sandboxPerks[entry];
    }
    detail.sandboxPerkCount = static_cast<std::uint8_t>(perks);
    const std::size_t overrides = row.renderOverrideCount < detail.renderOverrides.size()
                                      ? row.renderOverrideCount
                                      : detail.renderOverrides.size();
    for (std::size_t entry = 0; entry < overrides; ++entry) {
        detail.renderOverrides[entry] = {row.renderOverrides[entry].stage,
                                         row.renderOverrides[entry].key,
                                         row.renderOverrides[entry].value};
    }
    detail.renderOverrideCount = static_cast<std::uint8_t>(overrides);
    return detail;
}

// the constants blob has an 8-byte prefix before every offset the client quotes
constexpr std::size_t kConstantsPrefix = 8;
// client offset of the stat row the banner's power number is searched by
constexpr std::size_t kLightStatRowOffset = 592;
// native offset of the weapon Power stat row used by damage evaluation
constexpr std::size_t kWeaponPowerStatRowOffset = 606;
constexpr std::size_t kCharacterStatRowOffsets[]{593, 594, 595, 622, 623, 624};

} // namespace

bool collect_authored_hashes(AuthoredHashes& output) noexcept {
    output = {};
    const state::AccountState account = state::account_snapshot();
    if (!state::account::valid(account)) {
        return false;
    }
    const auto append = [&output](const state::account::inventory::Item& item) noexcept {
        if (output.count >= output.values.size()) {
            return false;
        }
        output.values[output.count++] = item.definitionHash;
        for (std::size_t lane = 0; lane < item.sockets.plugCount; ++lane) {
            if (!item.sockets.plugs[lane].has_value()) {
                continue;
            }
            if (output.count >= output.values.size()) {
                return false;
            }
            output.values[output.count++] = *item.sockets.plugs[lane];
        }
        return true;
    };
    for (std::size_t character = 0; character < account.characterCount; ++character) {
        for (const auto& item : account.characters[character].equipment.slots) {
            if (!item.has_value()) {
                continue;
            }
            if (!append(*item)) {
                return false;
            }
        }
        const state::account::inventory::CharacterItems& inventory =
            account.characters[character].inventory;
        for (std::size_t item = 0; item < inventory.count; ++item) {
            if (!append(inventory.values[item])) {
                return false;
            }
        }
    }
    const auto end = output.values.begin() + static_cast<std::ptrdiff_t>(output.count);
    std::sort(output.values.begin(), end);
    output.count =
        static_cast<std::size_t>(std::unique(output.values.begin(), end) - output.values.begin());
    return output.count != 0;
}

bool authored(const AuthoredHashes& hashes, std::uint32_t hash) noexcept {
    const auto begin = hashes.values.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(hashes.count);
    return std::binary_search(begin, end, hash);
}

bool equippable(const tables::items::Row& row) noexcept {
    return row.equipmentSlot.has_value() || equipment_slot(row.bucketId).has_value();
}

bool publish_buckets(Storage& storage) noexcept {
    namespace buckets = state::build_data::inventory::buckets;
    if (state::build_data::inventory_bucket_descriptors_ready()) {
        return true;
    }
    if (storage.bucketCount == 0 || storage.bucketCount > storage.bucketRows.size()) {
        return false;
    }
    bool hasEquipmentSlot = false;
    for (std::size_t index = 0; index < storage.bucketCount; ++index) {
        buckets::Descriptor& descriptor = storage.bucketRows[index];
        descriptor.equipmentSlot = storage.equipmentSlotByBucket[descriptor.bucketId];
        hasEquipmentSlot =
            hasEquipmentSlot || descriptor.equipmentSlot != buckets::kUnavailableEquipmentSlot;
    }
    return hasEquipmentSlot
           && state::build_data::publish_inventory_bucket_descriptors(
               std::span(storage.bucketRows).first(storage.bucketCount));
}

void request(std::uint16_t definitionIndex, DetailRequests& requested) noexcept {
    if (static_cast<std::size_t>(definitionIndex) < requested.size()) {
        requested.set(definitionIndex);
    }
}

void append_initial_plugs(const tables::items::Row& row,
                          std::uint64_t itemDefinitionCount,
                          DetailRequests& requested) noexcept {
    for (std::size_t lane = 0; lane < row.socketCount; ++lane) {
        if (row.initialPlugs[lane] == tables::items::kUnavailablePlug
            || row.initialPlugs[lane] >= itemDefinitionCount) {
            continue;
        }
        request(row.initialPlugs[lane], requested);
    }
}

bool materialize_requests(const DetailRequests& requested,
                          std::span<std::uint16_t> output,
                          std::size_t& count) noexcept {
    count = 0;
    for (std::size_t index = 0; index < requested.size(); ++index) {
        if (!requested.test(index)) {
            continue;
        }
        if (count >= output.size()) {
            count = 0;
            return false;
        }
        output[count++] = static_cast<std::uint16_t>(index);
    }
    return true;
}

bool build_detail(const DetailSource& source,
                  std::uint16_t definitionIndex,
                  domain::Definition& detail,
                  tables::items::Row& item) noexcept {
    tables::IndexRow indexRow{};
    item = {};
    item.definitionIndex = definitionIndex;
    if (!tables::index_row(source.table, source.array, definitionIndex, indexRow)
        || !reader::read_tag(
            *source.source, *source.scratch, indexRow.targetTag, *source.definition)
        || !tables::items::read_definition(std::span<const std::byte>{*source.definition}, item)) {
        return false;
    }
    item.definitionHash = indexRow.definitionHash;
    detail = to_detail(item);
    return true;
}

bool read_investment_constants(const reader::Source& source,
                               reader::Scratch& scratch,
                               std::span<const std::byte> root,
                               std::vector<std::byte>& blob,
                               state::build_data::constants::InvestmentConstants& output) noexcept {
    output = {};
    std::uint32_t tag = 0;
    if (!tables::slot_tag(root, tables::kInvestmentConstantsSlot, tag) || tag == 0
        || !reader::read_tag(source, scratch, tag, blob)) {
        return false;
    }
    const std::size_t last =
        kConstantsPrefix + kCharacterStatRowOffsets[std::size(kCharacterStatRowOffsets) - 1];
    if (blob.size() <= last) {
        return false;
    }
    output.lightStatRow =
        std::to_integer<std::uint8_t>(blob[kConstantsPrefix + kLightStatRowOffset]);
    output.weaponPowerStatRow =
        std::to_integer<std::uint8_t>(blob[kConstantsPrefix + kWeaponPowerStatRowOffset]);
    for (std::size_t row = 0; row < std::size(kCharacterStatRowOffsets); ++row) {
        output.characterStatRows[row] =
            std::to_integer<std::uint8_t>(blob[kConstantsPrefix + kCharacterStatRowOffsets[row]]);
    }
    output.extracted = output.weaponPowerStatRow < state::build_data::constants::kStatRowCount;
    return output.extracted;
}

} // namespace dawn::client::content::items::packages
