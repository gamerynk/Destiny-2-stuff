#include "family1_encoder.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "../character_record/layout.h"
#include "../family4/inventory/layout.h"

namespace dawn::middleware::datagen::family1 {
namespace {

namespace character_record = middleware::datagen::character_record;
namespace inventory = family4::inventory;

constexpr std::size_t kRootAccountOffset = 0x000;
constexpr std::size_t kRootCharacterOffset = 0x008;
constexpr std::size_t kRootProgressionsOffset = 0x010;

constexpr std::size_t kBodyIdentityOffset = 0x000;
constexpr std::size_t kBodyProgressionsOffset = 0x030;
constexpr std::size_t kBodyInventoryCountOffset = 0x820;
constexpr std::size_t kBodyInventoryOffset = 0x828;
constexpr std::size_t kBodyEquipmentOffset = 0xAA8;
constexpr std::size_t kBodyAuxiliaryCountOffset = 0xB48;
constexpr std::size_t kBodyAuxiliaryRowsOffset = 0xB4C;
constexpr std::size_t kBodyAuxiliaryRowSize = 0x0C;
constexpr std::size_t kBodyAuxiliaryRowCapacity = 20;
constexpr std::size_t kBodyOptionalPresentationAOffset = 0xC40;
constexpr std::size_t kBodyOptionalPresentationBOffset = 0xC60;
constexpr std::size_t kBodyAppearanceOffset = 0xC80;
constexpr std::size_t kBodySummaryOffset = 0x1B28;
constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFFU;

template <typename Value>
void write(std::span<std::byte> output, std::size_t offset, const Value& value) noexcept {
    std::memcpy(output.data() + offset, &value, sizeof value);
}

// empty values the three reflected PC 1.59 blocks need between equipment and render appearance.
// a cleared definition/hash is a real zero, and all-zero blocks can leave the remote model waiting
void apply_auxiliary_sentinels(std::span<std::byte> output) noexcept {
    // zero-count array of twenty 12-byte optional rows; keep the empty sentinels anyway
    write(output, kBodyAuxiliaryCountOffset, std::uint32_t{0});
    for (std::size_t index = 0; index < kBodyAuxiliaryRowCapacity; ++index) {
        const std::size_t row = kBodyAuxiliaryRowsOffset + index * kBodyAuxiliaryRowSize;
        write(output, row, kEmptyDefinitionIndex);
        write(output, row + 8U, character_record::layout::kNoHash);
    }

    // reflected handle 0x808079F6 has an optional definition index at +8
    write(output, kBodyOptionalPresentationAOffset + 8U, kEmptyDefinitionIndex);

    // handle 0x80807AE9: optional hash, three definition indices, nested optional index at +0x0C
    write(output,
          kBodyOptionalPresentationBOffset,
          character_record::layout::kNoHash);
    write(output, kBodyOptionalPresentationBOffset + 4U, kEmptyDefinitionIndex);
    write(output, kBodyOptionalPresentationBOffset + 6U, kEmptyDefinitionIndex);
    write(output, kBodyOptionalPresentationBOffset + 8U, kEmptyDefinitionIndex);
    write(output, kBodyOptionalPresentationBOffset + 0x0CU, kEmptyDefinitionIndex);
}

} // namespace

static_assert(kRootProgressionsOffset
                  + kProgressionCapacity * sizeof(family4::progression::layout::Entry)
              == kRootSize);
static_assert(kBodyProgressionsOffset
                  + kProgressionCapacity * sizeof(family4::progression::layout::Entry)
              == kBodyInventoryCountOffset);
static_assert(kBodyInventoryOffset + kInventoryCapacity * sizeof(inventory::layout::Entry)
              == kBodyEquipmentOffset);
static_assert(kBodyEquipmentOffset + kEquipmentCapacity * sizeof(std::uint64_t) == 0xB48);
static_assert(kBodyAppearanceOffset + character_record::layout::kAppearanceSize
              == kBodySummaryOffset);
static_assert(kBodySummaryOffset + character_record::layout::kSummarySize == kBodySize);

bool encode_root(
    std::uint64_t accountSoid,
    std::uint64_t characterSoid,
    const std::array<family4::progression::layout::Entry, kProgressionCapacity>& progressions,
    std::span<std::byte> output) noexcept {
    if (accountSoid == 0 || characterSoid == 0 || output.size() != kRootSize) {
        return false;
    }
    std::fill(output.begin(), output.end(), std::byte{});
    write(output, kRootAccountOffset, accountSoid);
    write(output, kRootCharacterOffset, characterSoid);
    std::memcpy(output.data() + kRootProgressionsOffset,
                progressions.data(),
                sizeof progressions);
    return true;
}

bool encode_body(
    std::uint64_t residentSoid,
    std::span<const std::byte> identity,
    const std::array<family4::progression::layout::Entry, kProgressionCapacity>& progressions,
    const family4::loadout::ResolvedLoadout& loadout,
    std::span<const std::byte> appearance,
    std::span<const std::byte> summary,
    std::span<std::byte> output) noexcept {
    if (residentSoid == 0 || identity.size() != character_record::layout::kIdentitySize
        || appearance.size() != character_record::layout::kAppearanceSize
        || summary.size() != character_record::layout::kSummarySize
        || output.size() != kBodySize || loadout.itemCount > loadout.items.size()) {
        return false;
    }

    std::array<inventory::layout::Entry, kInventoryCapacity> inventoryRows{};
    for (inventory::layout::Entry& row : inventoryRows) {
        row.definitionIndex = kEmptyDefinitionIndex;
    }
    std::array<std::uint64_t, kEquipmentCapacity> equipment{};
    std::array<bool, kEquipmentCapacity> occupiedSlots{};
    std::size_t equippedCount = 0;
    for (std::size_t index = 0; index < loadout.itemCount; ++index) {
        const family4::loadout::ResolvedItem& item = loadout.items[index];
        if (!item.equipped) {
            continue;
        }
        if (equippedCount >= inventoryRows.size() || item.equipmentSlot >= equipment.size()
            || occupiedSlots[item.equipmentSlot] || item.instance.instanceSoid == 0
            || item.instance.baseDefinitionIndex == kEmptyDefinitionIndex) {
            return false;
        }
        inventory::layout::Entry& row = inventoryRows[equippedCount++];
        row.definitionIndex = item.instance.baseDefinitionIndex;
        row.instanceSoid = item.instance.instanceSoid;
        row.quantity = item.quantity;
        row.mutationSerial = item.mutationSerial;
        row.flags = item.flags;
        equipment[item.equipmentSlot] = item.instance.instanceSoid;
        occupiedSlots[item.equipmentSlot] = true;
    }

    std::fill(output.begin(), output.end(), std::byte{});
    std::memcpy(output.data() + kBodyIdentityOffset, identity.data(), identity.size());
    // QueueZ needs the serialized SOID to match the object header. PC 1.59 asks for slot one by the
    // account/root SOID; the character stays linked by root +8, identity +8 onward is intact
    write(output, kBodyIdentityOffset, residentSoid);
    std::memcpy(output.data() + kBodyProgressionsOffset,
                progressions.data(),
                sizeof progressions);
    const std::uint32_t nativeEquippedCount = static_cast<std::uint32_t>(equippedCount);
    write(output, kBodyInventoryCountOffset, nativeEquippedCount);
    std::memcpy(output.data() + kBodyInventoryOffset,
                inventoryRows.data(),
                sizeof inventoryRows);
    std::memcpy(output.data() + kBodyEquipmentOffset, equipment.data(), sizeof equipment);
    apply_auxiliary_sentinels(output);
    std::memcpy(output.data() + kBodyAppearanceOffset, appearance.data(), appearance.size());
    std::memcpy(output.data() + kBodySummaryOffset, summary.data(), summary.size());
    return true;
}

} // namespace dawn::middleware::datagen::family1
