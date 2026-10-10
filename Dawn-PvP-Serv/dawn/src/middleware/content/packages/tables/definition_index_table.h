#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::content::packages::tables {

inline constexpr std::uint32_t kTagLowerBound = 0x80800000;
inline constexpr std::uint32_t kTagUpperBound = 0x82000000;
// entry index in the low bits, package id above them
inline constexpr std::uint32_t kTagPackageShift = 13;
inline constexpr std::uint16_t kAbsentPackageId = 0xFFFFU;

[[nodiscard]] constexpr std::uint16_t package_of(std::uint32_t tag) noexcept {
    if (tag < kTagLowerBound || tag >= kTagUpperBound) {
        return kAbsentPackageId;
    }
    return static_cast<std::uint16_t>((tag - kTagLowerBound) >> kTagPackageShift);
}

inline constexpr std::uint32_t kItemIndexTableClass = 0x80807BE8U;
inline constexpr std::size_t kCollectibleTableSlot = 19;
inline constexpr std::uint32_t kInvestmentRootClass = 0x80807D84U;
inline constexpr std::uint32_t kCollectibleTableClass = 0x8080306DU;
inline constexpr std::uint32_t kCollectibleRowClass = 0x80803475U;
inline constexpr std::size_t kCollectibleRowStride = 0xB8;
inline constexpr std::size_t kCollectibleHashOffset = 0x28;
inline constexpr std::size_t kCollectibleItemIndexOffset = 0x2C;
inline constexpr std::size_t kCollectibleMaterialRequirementIndexOffset = 0x9A;
inline constexpr std::size_t kMaterialRequirementTableSlot = 96;
inline constexpr std::uint32_t kMaterialRequirementTableClass = 0x80807ACEU;
inline constexpr std::uint32_t kMaterialRequirementSetRowClass = 0x80807AD4U;
inline constexpr std::uint32_t kMaterialRequirementRowClass = 0x80807AD7U;
// set row is a hash then a self-relative pointer to an array descriptor
inline constexpr std::size_t kMaterialRequirementSetRowStride = 0x10;
inline constexpr std::size_t kMaterialRequirementSetHashOffset = 0;
inline constexpr std::size_t kMaterialRequirementSetArrayPointerOffset = 8;
// requirement row: item index, quantity, two flags, native sentinel
inline constexpr std::size_t kMaterialRequirementRowStride = 0x0C;
inline constexpr std::size_t kMaterialRequirementItemIndexOffset = 0;
inline constexpr std::size_t kMaterialRequirementQuantityOffset = 4;
inline constexpr std::size_t kMaterialRequirementDeleteOffset = 8;
inline constexpr std::size_t kMaterialRequirementOmitOffset = 9;
inline constexpr std::size_t kMaterialRequirementSentinelOffset = 10;
inline constexpr std::size_t kInsertionMaterialRequirementSetIndexOffset = 0x1E8;
inline constexpr std::size_t kEnabledMaterialRequirementSetIndexOffset = 0x200;
inline constexpr std::uint16_t kUnavailableMaterialRequirementSetIndex = 0xFFFFU;
inline constexpr std::uint32_t kOrdinarySocketClass = 0x808077C4U;
inline constexpr std::size_t kSocketEntryStride = 80;
inline constexpr std::size_t kSocketBlockOffset = 104;
inline constexpr std::size_t kBucketIdOffset = 184;
inline constexpr std::size_t kArtBlockOffset = 136;
inline constexpr std::uint32_t kArtRowClass = 0x808077B5U;
inline constexpr std::uint32_t kMaterialOverrideClass = 0x808077B3U;
inline constexpr std::uint32_t kSandboxPerkClass = 0x808077BCU;
inline constexpr std::size_t kStatBlockOffset = 112;
inline constexpr std::size_t kInvestmentConstantsSlot = 11;
inline constexpr std::size_t kProgressionTableSlot = 68;
inline constexpr std::uint32_t kProgressionTableClass = 0x80807CDDU;
inline constexpr std::size_t kProgressionRowStride = 88;
inline constexpr std::size_t kProgressionScopeOffset = 4;

struct Array {
    std::uint64_t count{};
    std::size_t dataOffset{};
    std::uint32_t elementClass{};
};

struct IndexRow {
    std::uint32_t definitionHash{};
    std::uint32_t targetTag{};
};

using RowVisitor = bool (*)(void* context, std::uint32_t index, const IndexRow& row) noexcept;

[[nodiscard]] bool find_array_at(std::span<const std::byte> blob,
                                 std::size_t descriptorOffset,
                                 Array& output) noexcept;

[[nodiscard]] bool
find_array(std::span<const std::byte> blob, std::uint32_t elementClass, Array& output) noexcept;

[[nodiscard]] bool visit_index_rows(std::span<const std::byte> blob,
                                    const Array& array,
                                    RowVisitor visitor,
                                    void* context) noexcept;

[[nodiscard]] bool
child_tag(std::span<const std::byte> blob, std::size_t index, std::uint32_t& tag) noexcept;

[[nodiscard]] bool
slot_tag(std::span<const std::byte> blob, std::size_t index, std::uint32_t& tag) noexcept;

inline constexpr std::size_t kItemTableSlot = 48;
inline constexpr std::size_t kPlugSetTableSlot = 51;
inline constexpr std::size_t kSocketEntryListTableSlot = 97;
inline constexpr std::size_t kInvestmentRootChild = 0;
inline constexpr std::size_t kBucketTableSlot = 17;
// exactly one installed entry has this class, so a class sweep finds the table without a fixed tag
inline constexpr std::uint32_t kBucketDefinitionTableClass = 0x80805936U;
inline constexpr std::size_t kBucketDefinitionCount = 34;
inline constexpr std::size_t kBucketDefinitionSize = 72;
inline constexpr std::size_t kBucketDefinitionEquipmentSlotOffset = 64;
inline constexpr std::uint8_t kBucketDefinitionUnavailableEquipmentSlot = 0xFF;
inline constexpr std::uint32_t kSocketEntryListTableClass = 0x80807A7EU;
inline constexpr std::size_t kSocketEntryArrayDescriptor = 16;
inline constexpr std::size_t kSocketEntrySize = 64;
inline constexpr std::size_t kSocketEntryPlugSource = 8;
inline constexpr std::size_t kSocketEntryGroup = 12;
// kind 34 is the super lane, active despite having no plug source
inline constexpr std::size_t kSocketEntryKind = 13;
inline constexpr std::uint32_t kNoPlugSource = 0x811C9DC5U;
inline constexpr std::size_t kBucketCountOffset = 140;
inline constexpr std::size_t kBucketFirstDescriptor = 144;
inline constexpr std::size_t kBucketDescriptorSize = 36;
inline constexpr std::size_t kBucketFirstSlotOffset = 4;
inline constexpr std::size_t kBucketSlotCountOffset = 8;
inline constexpr std::size_t kBucketArraySelectorOffset = 24;

inline constexpr std::size_t kTableArrayDescriptor = 8;

[[nodiscard]] std::size_t child_count(std::span<const std::byte> blob) noexcept;

[[nodiscard]] bool index_row(std::span<const std::byte> blob,
                             const Array& array,
                             std::uint64_t index,
                             IndexRow& row) noexcept;

} // namespace dawn::middleware::content::packages::tables
