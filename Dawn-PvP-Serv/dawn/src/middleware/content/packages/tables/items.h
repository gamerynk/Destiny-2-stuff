#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dawn::middleware::content::packages::tables::items {

inline constexpr std::size_t kSocketCapacity = 12;
inline constexpr std::uint16_t kUnavailablePlug = 0xFFFF;
inline constexpr std::uint16_t kUnavailableMaterialRequirementSetIndex = 0xFFFFU;

inline constexpr std::size_t kStatCapacity = 64;
inline constexpr std::size_t kSandboxPerkCapacity = 4;
inline constexpr std::size_t kRenderOverrideCapacity = 32;
inline constexpr std::uint16_t kUnavailableArtIndex = 0xFFFF;
// generic art plus one row each for Titan, Hunter and Warlock
inline constexpr std::size_t kArtClassCapacity = 4;
inline constexpr std::uint16_t kUnavailableSocketType = 0xFFFF;
inline constexpr std::int8_t kEmptyOverrideKey = -1;

struct RenderOverride {
    std::uint8_t stage{};
    std::int8_t key{kEmptyOverrideKey};
    std::uint16_t value{};
};

struct Row {
    std::uint32_t definitionHash{};
    std::uint16_t definitionIndex{};
    std::uint8_t bucketId{};
    std::int32_t maxStackSize{};
    bool instanced{};
    std::optional<std::int8_t> equipmentSlot{};
    std::int32_t rawEquipmentSlot{};
    bool hasSocketEntryList{};
    std::uint16_t socketEntryListIndex{};
    bool hasSockets{};
    std::uint8_t socketCount{};
    std::uint16_t initialPlugs[kSocketCapacity]{};
    std::uint16_t socketTypes[kSocketCapacity]{};
    // a few native sockets use this to expand a seed into its whole safe family
    std::uint32_t plugCategoryHash{};
    std::uint16_t insertionMaterialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
    std::uint16_t enabledMaterialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
    std::uint8_t statCount{};
    std::uint8_t statRows[kStatCapacity]{};
    std::int32_t statValues[kStatCapacity]{};
    std::uint16_t gearArtIndex{kUnavailableArtIndex};
    std::uint16_t artArrangementIndices[kArtClassCapacity]{};
    std::uint8_t sandboxPerkCount{};
    std::uint16_t sandboxPerks[kSandboxPerkCapacity]{};
    std::uint8_t renderOverrideCount{};
    RenderOverride renderOverrides[kRenderOverrideCapacity]{};
};

// these sit behind self-relative blob offsets; valid only against the serialized definition
void read_appearance(std::span<const std::byte> definition, Row& row) noexcept;

[[nodiscard]] bool read_definition(std::span<const std::byte> definition, Row& row) noexcept;

using AllowedPlugVisitor = bool (*)(void* context, std::uint32_t itemDefinitionIndex) noexcept;

// the initial plug is a separate fixed field, left to the caller
[[nodiscard]] bool visit_allowed_plugs(std::span<const std::byte> definition,
                                       std::span<const std::byte> plugSetTable,
                                       std::uint8_t lane,
                                       AllowedPlugVisitor visitor,
                                       void* context) noexcept;

} // namespace dawn::middleware::content::packages::tables::items
