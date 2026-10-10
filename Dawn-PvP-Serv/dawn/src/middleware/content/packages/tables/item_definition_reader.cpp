#include <algorithm>
#include <cstring>

#include "definition_index_table.h"
#include "items.h"

namespace dawn::middleware::content::packages::tables::items {
namespace {

constexpr std::size_t kMaxStackSizeOffset = 180;
// nonzero predicate byte marks an instanced definition
constexpr std::size_t kInstancedOffset = 187;
constexpr std::size_t kEquipmentBlockOffset = 16;
constexpr std::size_t kEquipmentSlotOffset = 24;
constexpr std::size_t kSocketEntryListBlockOffset = 128;
constexpr std::size_t kSocketEntryListBlockSize = 12;
constexpr std::int32_t kEquipmentSlotCount = 20;
constexpr std::size_t kSocketTypeOffset = 0;
constexpr std::size_t kSocketPlugOffset = 2;
constexpr std::size_t kFixedFieldEnd = kInstancedOffset + 1;
// optional plug category, used to expand three native reusable plug families
constexpr std::size_t kPlugCategoryOffset = 392;
constexpr std::size_t kEmbeddedPlugListOffset = 64;
constexpr std::size_t kReusablePlugSetIndexOffset = 12;
constexpr std::size_t kRandomizedPlugSetIndexOffset = 32;
constexpr std::size_t kPlugSetRowStride = 24;
constexpr std::size_t kPlugSetMemberDescriptorOffset = 8;
constexpr std::size_t kPlugMemberStride = 32;
constexpr std::size_t kPlugMemberIndexOffset = 0;
// native arrays use 16-bit definition indices even though the serialized field is 32-bit
constexpr std::uint64_t kMaximumPlugMemberCount = 65535;

template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

void read_socket_entry_list(std::span<const std::byte> definition, Row& row) noexcept {
    row.hasSocketEntryList = false;
    row.socketEntryListIndex = 0;
    std::int64_t relative = 0;
    if (!read(definition, kSocketEntryListBlockOffset, relative) || relative == 0) {
        return;
    }
    const std::int64_t block = static_cast<std::int64_t>(kSocketEntryListBlockOffset) + relative;
    if (block < 0) {
        return;
    }
    const auto blockOffset = static_cast<std::uint64_t>(block);
    if (blockOffset + kSocketEntryListBlockSize > definition.size()) {
        return;
    }
    std::uint16_t index = 0;
    if (read(definition, static_cast<std::size_t>(block), index)) {
        row.hasSocketEntryList = true;
        row.socketEntryListIndex = index;
    }
}

void read_equipment_slot(std::span<const std::byte> definition,
                         std::optional<std::int8_t>& slot,
                         std::int32_t& raw) noexcept {
    slot.reset();
    raw = -1;
    std::int64_t relative = 0;
    if (!read(definition, kEquipmentBlockOffset, relative) || relative == 0) {
        return;
    }
    const std::int64_t block = static_cast<std::int64_t>(kEquipmentBlockOffset) + relative;
    if (block < 0) {
        return;
    }
    // a value outside the equippable range means the item takes no slot
    std::int32_t value = 0;
    if (read(definition, static_cast<std::size_t>(block) + kEquipmentSlotOffset, value)
        && value >= 0 && value < static_cast<std::int32_t>(kEquipmentSlotCount)) {
        slot = static_cast<std::int8_t>(value);
    }
}

// socket type index picks the overflow-hash priority the character record applies
void read_sockets(std::span<const std::byte> definition, Row& row) noexcept {
    row.hasSockets = false;
    row.socketCount = 0;
    std::fill(std::begin(row.initialPlugs), std::end(row.initialPlugs), kUnavailablePlug);
    std::fill(std::begin(row.socketTypes), std::end(row.socketTypes), kUnavailableSocketType);
    std::int64_t relative = 0;
    if (!read(definition, kSocketBlockOffset, relative) || relative == 0) {
        return;
    }
    const std::int64_t block = static_cast<std::int64_t>(kSocketBlockOffset) + relative;
    if (block < 0 || static_cast<std::uint64_t>(block) >= definition.size()) {
        return;
    }
    Array array{};
    if (!find_array_at(definition, static_cast<std::size_t>(block), array)
        || array.elementClass != kOrdinarySocketClass) {
        return;
    }
    row.hasSockets = true;
    const auto lanes =
        static_cast<std::size_t>((std::min)(array.count, std::uint64_t{kSocketCapacity}));
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        const std::size_t entry = array.dataOffset + lane * kSocketEntryStride;
        std::uint16_t plug = kUnavailablePlug;
        std::uint16_t type = kUnavailableSocketType;
        if (!read(definition, entry + kSocketPlugOffset, plug)
            || !read(definition, entry + kSocketTypeOffset, type)) {
            return;
        }
        row.initialPlugs[lane] = plug;
        row.socketTypes[lane] = type;
        row.socketCount = static_cast<std::uint8_t>(lane + 1);
    }
}

[[nodiscard]] bool visit_plug_array(std::span<const std::byte> blob,
                                    const Array& array,
                                    AllowedPlugVisitor visitor,
                                    void* context) noexcept {
    if (array.count > kMaximumPlugMemberCount || array.dataOffset > blob.size()
        || array.count > (blob.size() - array.dataOffset) / kPlugMemberStride) {
        return false;
    }
    for (std::uint64_t index = 0; index < array.count; ++index) {
        std::uint32_t itemDefinitionIndex = 0;
        const std::size_t row =
            array.dataOffset + static_cast<std::size_t>(index) * kPlugMemberStride;
        if (!read(blob, row + kPlugMemberIndexOffset, itemDefinitionIndex)
            || !visitor(context, itemDefinitionIndex)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool visit_shared_plug_set(std::span<const std::byte> definition,
                                         std::size_t socketEntry,
                                         std::size_t setIndexOffset,
                                         std::span<const std::byte> plugSetTable,
                                         const Array& sets,
                                         AllowedPlugVisitor visitor,
                                         void* context) noexcept {
    std::uint16_t setIndex = kUnavailablePlug;
    if (!read(definition, socketEntry + setIndexOffset, setIndex)) {
        return false;
    }
    if (setIndex == kUnavailablePlug) {
        return true;
    }
    if (setIndex >= sets.count || sets.dataOffset > plugSetTable.size()
        || sets.count > (plugSetTable.size() - sets.dataOffset) / kPlugSetRowStride) {
        return false;
    }
    const std::size_t descriptor = sets.dataOffset
                                   + static_cast<std::size_t>(setIndex) * kPlugSetRowStride
                                   + kPlugSetMemberDescriptorOffset;
    std::uint64_t memberCount = 0;
    if (!read(plugSetTable, descriptor, memberCount)) {
        return false;
    }
    if (memberCount == 0) {
        return true;
    }
    Array members{};
    return find_array_at(plugSetTable, descriptor, members)
           && visit_plug_array(plugSetTable, members, visitor, context);
}

constexpr std::size_t kStatDataMember = 8;
constexpr std::size_t kStatEntryStride = 40;
constexpr std::size_t kStatEntryRow = 16;
constexpr std::size_t kStatEntryValue = 20;

// two self-relative hops (definition -> header -> entries); blob offsets, not process memory
void read_stats(std::span<const std::byte> definition, Row& row) noexcept {
    row.statCount = 0;
    std::int64_t blockRelative = 0;
    if (!read(definition, kStatBlockOffset, blockRelative)) {
        return;
    }
    const auto header = static_cast<std::int64_t>(kStatBlockOffset) + blockRelative;
    if (header < 0 || static_cast<std::size_t>(header) + kStatDataMember > definition.size()) {
        return;
    }

    std::uint64_t count = 0;
    std::int64_t dataRelative = 0;
    if (!read(definition, static_cast<std::size_t>(header), count) || count == 0
        || count > kStatCapacity
        || !read(definition, static_cast<std::size_t>(header) + kStatDataMember, dataRelative)) {
        return;
    }
    const std::int64_t data = header + static_cast<std::int64_t>(kStatDataMember) + dataRelative;
    if (data < 0 || static_cast<std::size_t>(data) + kStatEntryStride * count > definition.size()) {
        return;
    }

    for (std::uint64_t entry = 0; entry < count; ++entry) {
        const auto base = static_cast<std::size_t>(data) + kStatEntryStride * entry;
        if (!read(definition, base + kStatEntryRow, row.statRows[entry])
            || !read(definition, base + kStatEntryValue, row.statValues[entry])) {
            row.statCount = 0;
            return;
        }
    }
    row.statCount = static_cast<std::uint8_t>(count);
}

} // namespace

bool read_definition(std::span<const std::byte> definition, Row& row) noexcept {
    const std::uint32_t hash = row.definitionHash;
    const std::uint16_t index = row.definitionIndex;
    row = {};
    row.definitionHash = hash;
    row.definitionIndex = index;
    row.insertionMaterialRequirementSetIndex = kUnavailableMaterialRequirementSetIndex;
    row.enabledMaterialRequirementSetIndex = kUnavailableMaterialRequirementSetIndex;
    std::fill(std::begin(row.initialPlugs), std::end(row.initialPlugs), kUnavailablePlug);
    std::fill(std::begin(row.socketTypes), std::end(row.socketTypes), kUnavailableSocketType);
    if (definition.size() < kFixedFieldEnd) {
        return false;
    }
    std::uint8_t instanced = 0;
    if (!read(definition, kBucketIdOffset, row.bucketId)
        || !read(definition, kMaxStackSizeOffset, row.maxStackSize)
        || !read(definition, kInstancedOffset, instanced)) {
        return false;
    }
    row.instanced = instanced != 0;
    // short legacy definitions don't declare a plug category
    (void)read(definition, kPlugCategoryOffset, row.plugCategoryHash);
    (void)read(definition,
               kInsertionMaterialRequirementSetIndexOffset,
               row.insertionMaterialRequirementSetIndex);
    (void)read(definition,
               kEnabledMaterialRequirementSetIndexOffset,
               row.enabledMaterialRequirementSetIndex);
    read_stats(definition, row);
    read_appearance(definition, row);
    read_socket_entry_list(definition, row);
    read_equipment_slot(definition, row.equipmentSlot, row.rawEquipmentSlot);
    read_sockets(definition, row);
    return true;
}

bool visit_allowed_plugs(std::span<const std::byte> definition,
                         std::span<const std::byte> plugSetTable,
                         std::uint8_t lane,
                         AllowedPlugVisitor visitor,
                         void* context) noexcept {
    if (visitor == nullptr || lane >= kSocketCapacity) {
        return false;
    }
    std::int64_t socketBlockRelative = 0;
    if (!read(definition, kSocketBlockOffset, socketBlockRelative) || socketBlockRelative == 0) {
        return false;
    }
    const std::int64_t socketBlock =
        static_cast<std::int64_t>(kSocketBlockOffset) + socketBlockRelative;
    if (socketBlock < 0 || static_cast<std::uint64_t>(socketBlock) >= definition.size()) {
        return false;
    }
    Array sockets{};
    if (!find_array_at(definition, static_cast<std::size_t>(socketBlock), sockets)
        || sockets.elementClass != kOrdinarySocketClass || lane >= sockets.count
        || sockets.dataOffset > definition.size()
        || sockets.count > (definition.size() - sockets.dataOffset) / kSocketEntryStride) {
        return false;
    }
    Array sets{};
    if (!find_array_at(plugSetTable, kTableArrayDescriptor, sets)) {
        return false;
    }
    const std::size_t socketEntry =
        sockets.dataOffset + static_cast<std::size_t>(lane) * kSocketEntryStride;
    Array embedded{};
    std::uint64_t embeddedCount = 0;
    if (!read(definition, socketEntry + kEmbeddedPlugListOffset, embeddedCount)
        || (embeddedCount != 0
            && (!find_array_at(definition, socketEntry + kEmbeddedPlugListOffset, embedded)
                || !visit_plug_array(definition, embedded, visitor, context)))) {
        return false;
    }
    return visit_shared_plug_set(definition,
                                 socketEntry,
                                 kReusablePlugSetIndexOffset,
                                 plugSetTable,
                                 sets,
                                 visitor,
                                 context)
           && visit_shared_plug_set(definition,
                                    socketEntry,
                                    kRandomizedPlugSetIndexOffset,
                                    plugSetTable,
                                    sets,
                                    visitor,
                                    context);
}

} // namespace dawn::middleware::content::packages::tables::items
