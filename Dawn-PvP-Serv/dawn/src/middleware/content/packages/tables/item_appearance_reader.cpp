#include <cstring>

#include "definition_index_table.h"
#include "items.h"

namespace dawn::middleware::content::packages::tables::items {
namespace {

constexpr std::size_t kGearArtIndexOffset = 88;
// art row: signed class, one pad byte, art index
constexpr std::size_t kArtRowStride = 4;
constexpr std::size_t kArtRowValueOffset = 2;
// material override arrays, one per stage
constexpr std::size_t kMaterialStageOffsets[]{40, 56, 72};
constexpr std::size_t kMaterialRowStride = 4;
constexpr std::size_t kMaterialRowValueOffset = 2;
constexpr std::size_t kSandboxPerkDescriptor = 16;
constexpr std::size_t kSandboxPerkStride = 24;
constexpr std::uint16_t kUnavailableSandboxPerk = 0xFFFF;

template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

[[nodiscard]] bool resolve_block(std::span<const std::byte> definition,
                                 std::size_t member,
                                 std::size_t& block) noexcept {
    std::int64_t relative = 0;
    if (!read(definition, member, relative) || relative == 0) {
        return false;
    }
    const std::int64_t resolved = static_cast<std::int64_t>(member) + relative;
    if (resolved < 0 || static_cast<std::uint64_t>(resolved) >= definition.size()) {
        return false;
    }
    block = static_cast<std::size_t>(resolved);
    return true;
}

void read_art(std::span<const std::byte> definition, std::size_t art, Row& row) noexcept {
    (void)read(definition, art + kGearArtIndexOffset, row.gearArtIndex);
    Array rows{};
    if (!find_array_at(definition, art, rows) || rows.elementClass != kArtRowClass) {
        return;
    }
    for (std::uint64_t index = 0; index < rows.count; ++index) {
        const std::size_t at = rows.dataOffset + static_cast<std::size_t>(index) * kArtRowStride;
        std::int8_t characterClass = -1;
        std::uint16_t arrangement = kUnavailableArtIndex;
        if (!read(definition, at, characterClass)
            || !read(definition, at + kArtRowValueOffset, arrangement)) {
            return;
        }
        const std::size_t slot =
            characterClass == -1 ? 0 : static_cast<std::size_t>(characterClass) + 1U;
        if (slot < kArtClassCapacity && row.artArrangementIndices[slot] == kUnavailableArtIndex) {
            row.artArrangementIndices[slot] = arrangement;
        }
    }
}

// keep stage order; the consumer folds base and plug rows stage by stage. empty key is not a row
void read_material_overrides(std::span<const std::byte> definition,
                             std::size_t art,
                             Row& row) noexcept {
    for (std::size_t stage = 0; stage < std::size(kMaterialStageOffsets); ++stage) {
        Array overrides{};
        if (!find_array_at(definition, art + kMaterialStageOffsets[stage], overrides)
            || overrides.elementClass != kMaterialOverrideClass) {
            continue;
        }
        for (std::uint64_t entry = 0; entry < overrides.count; ++entry) {
            if (row.renderOverrideCount >= kRenderOverrideCapacity) {
                return;
            }
            const std::size_t at =
                overrides.dataOffset + static_cast<std::size_t>(entry) * kMaterialRowStride;
            std::int8_t key = kEmptyOverrideKey;
            std::uint16_t value = 0;
            if (!read(definition, at, key)
                || !read(definition, at + kMaterialRowValueOffset, value)) {
                return;
            }
            if (key == kEmptyOverrideKey) {
                continue;
            }
            row.renderOverrides[row.renderOverrideCount++] = {
                static_cast<std::uint8_t>(stage), key, value};
        }
    }
}

// these are sandbox perk table rows, not item indices; the character record wants that form
void read_sandbox_perks(std::span<const std::byte> definition, Row& row) noexcept {
    std::size_t block = 0;
    Array perks{};
    if (!resolve_block(definition, kStatBlockOffset, block)
        || !find_array_at(definition, block + kSandboxPerkDescriptor, perks)
        || perks.elementClass != kSandboxPerkClass) {
        return;
    }
    for (std::uint64_t entry = 0; entry < perks.count; ++entry) {
        if (row.sandboxPerkCount >= kSandboxPerkCapacity) {
            return;
        }
        std::uint16_t perk = kUnavailableSandboxPerk;
        if (!read(definition,
                  perks.dataOffset + static_cast<std::size_t>(entry) * kSandboxPerkStride,
                  perk)) {
            return;
        }
        if (perk != kUnavailableSandboxPerk) {
            row.sandboxPerks[row.sandboxPerkCount++] = perk;
        }
    }
}

} // namespace

void read_appearance(std::span<const std::byte> definition, Row& row) noexcept {
    row.gearArtIndex = kUnavailableArtIndex;
    std::fill(std::begin(row.artArrangementIndices),
              std::end(row.artArrangementIndices),
              kUnavailableArtIndex);
    row.sandboxPerkCount = 0;
    row.renderOverrideCount = 0;
    std::size_t art = 0;
    // no art block means nothing drawn and no overrides (subclasses, emblems)
    if (resolve_block(definition, kArtBlockOffset, art)) {
        read_art(definition, art, row);
        read_material_overrides(definition, art, row);
    }
    read_sandbox_perks(definition, row);
}

} // namespace dawn::middleware::content::packages::tables::items
