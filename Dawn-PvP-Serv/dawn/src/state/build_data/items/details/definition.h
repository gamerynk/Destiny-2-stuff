#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace dawn::state::build_data::items::details {

// the supported build has 15,424 item rows, so the next power of two bounds the detail closure
inline constexpr std::size_t kDefinitionCapacity = 16384;
inline constexpr std::size_t kInitialPlugCapacity = 12;
inline constexpr std::size_t kEquipmentSlotCount = 20;
inline constexpr std::uint16_t kUnavailableItemIndex = 0xFFFF;
inline constexpr std::uint16_t kEmptySocketEntryListIndex = 0;
inline constexpr std::size_t kStatCapacity = 16;
inline constexpr std::uint8_t kEmptyStatRow = 0xFF;
inline constexpr std::size_t kSandboxPerkCapacity = 4;
inline constexpr std::size_t kRenderOverrideCapacity = 32;
inline constexpr std::uint16_t kUnavailableArtIndex = 0xFFFF;
inline constexpr std::size_t kArtClassCapacity = 4;
inline constexpr std::uint16_t kUnavailableSocketType = 0xFFFF;
inline constexpr std::int8_t kEmptyOverrideKey = -1;

struct Stat {
    std::uint8_t row{kEmptyStatRow};
    std::int32_t value{};
};

struct RenderOverride {
    std::uint8_t stage{};
    std::int8_t key{kEmptyOverrideKey};
    std::uint16_t value{};
};

[[nodiscard]] constexpr std::array<std::uint16_t, 12> unavailable_socket_types() noexcept {
    std::array<std::uint16_t, 12> result{};
    result.fill(kUnavailableSocketType);
    return result;
}

enum class OrdinarySocketState : std::uint8_t {
    absent = 0,
    present = 1,
};

enum class InstancedDefinitionState : std::uint8_t {
    stackable = 0,
    instanced = 1,
};

[[nodiscard]] constexpr std::array<std::uint16_t, kInitialPlugCapacity>
unavailable_plug_indices() noexcept {
    std::array<std::uint16_t, kInitialPlugCapacity> result{};
    result.fill(kUnavailableItemIndex);
    return result;
}

[[nodiscard]] constexpr std::array<std::uint16_t, kArtClassCapacity>
unavailable_art_indices() noexcept {
    std::array<std::uint16_t, kArtClassCapacity> result{};
    result.fill(kUnavailableArtIndex);
    return result;
}

struct Definition {
    std::uint16_t definitionIndex{};
    std::uint32_t definitionHash{};
    std::uint8_t bucketId{};
    std::int32_t maxStackSize{};
    InstancedDefinitionState instancedDefinitionState{InstancedDefinitionState::stackable};
    std::optional<std::int8_t> equipmentSlot{};
    OrdinarySocketState ordinarySocketState{OrdinarySocketState::absent};
    std::uint8_t ordinarySocketCount{};
    std::array<std::uint16_t, kInitialPlugCapacity> initialPlugIndices{unavailable_plug_indices()};
    std::array<std::uint16_t, kInitialPlugCapacity> socketTypes{unavailable_socket_types()};
    std::uint16_t socketEntryListIndex{kEmptySocketEntryListIndex};
    std::uint8_t statCount{};
    std::array<Stat, kStatCapacity> stats{};
    std::uint16_t gearArtIndex{kUnavailableArtIndex};
    std::array<std::uint16_t, kArtClassCapacity> artArrangementIndices{unavailable_art_indices()};
    std::uint8_t sandboxPerkCount{};
    std::array<std::uint16_t, kSandboxPerkCapacity> sandboxPerks{};
    std::uint8_t renderOverrideCount{};
    std::array<RenderOverride, kRenderOverrideCapacity> renderOverrides{};
};

} // namespace dawn::state::build_data::items::details
