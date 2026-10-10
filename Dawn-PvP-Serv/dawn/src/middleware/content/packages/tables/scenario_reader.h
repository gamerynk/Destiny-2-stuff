#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace dawn::middleware::content::packages::tables {

inline constexpr std::uint32_t kScenarioClass = 0x80809994U;
inline constexpr std::size_t kScenarioBubbleDescriptor = 80;
inline constexpr std::uint32_t kBubbleClass = 0x8080924DU;
inline constexpr std::size_t kBubbleStride = 24;
inline constexpr std::size_t kBubbleStateDescriptor = 8;
inline constexpr std::uint32_t kSliceStateClass = 0x8080924FU;
inline constexpr std::size_t kSliceStateStride = 76;
inline constexpr std::uint32_t kSliceEntryClass = 0x8080925BU;
inline constexpr std::size_t kSliceEntryIndexOffset = 16;
inline constexpr std::size_t kSliceEntryRegistryOffset = 20;
inline constexpr std::uint32_t kSliceSetIndexFactor = 8;
// the bit a container bubble mask sets; not the scenario ordinal, bubbles are shared by every activity on the map
inline constexpr std::size_t kStateMapBubbleIndexOffset = 28;
inline constexpr std::uint16_t kAbsentMapBubbleIndex = 0xFFFFU;
inline constexpr std::uint32_t kObjectRegistryClass = 0x8080925EU;
// three independent object tag arrays
inline constexpr std::size_t kRegistryFirstDescriptor = 8;
inline constexpr std::size_t kRegistrySecondDescriptor = 24;
inline constexpr std::size_t kRegistryThirdDescriptor = 40;
inline constexpr std::uint32_t kRegistryElementClass = 0x80809260U;
inline constexpr std::size_t kRegistryElementStride = 4;
inline constexpr std::uint32_t kObjectClass = 0x80809462U;
inline constexpr std::size_t kObjectKeyOffset = 12;
inline constexpr std::size_t kObjectSlotDescriptor = 32;
inline constexpr std::size_t kSlotStride = 8;
inline constexpr std::size_t kObjectBubbleDescriptor = 56;
inline constexpr std::size_t kObjectBubbleStride = 24;
inline constexpr std::size_t kObjectBubbleHandleDescriptor = 8;
inline constexpr std::size_t kObjectPlacedHandleStride = 4;
inline constexpr std::int32_t kGlobalBubbleIndex = -1;

struct Slot {
    std::uint32_t type{};
    std::uint32_t nameHash{};
};

struct ObjectBubble {
    std::int32_t bubbleIndex{};
    std::uint64_t handleCount{};
    std::size_t handleDataOffset{};
};

struct Bubble {
    std::uint32_t nameHash{};
    std::uint64_t stateCount{};
    std::size_t stateDataOffset{};
};

struct SliceState {
    std::uint32_t stateHash{};
    std::uint32_t bubbleNameHash{};
    std::uint32_t entryTag{};
    std::uint32_t mapBubbleIndex{};
    bool enabled{};
    // client prints PUB when the first state carries this, else PRV
    bool isPublic{};
};

struct SliceEntry {
    std::uint32_t index{};
    std::uint32_t registryTag{};
};

[[nodiscard]] bool scenario_bubbles(std::span<const std::byte> blob, Array& output) noexcept;

[[nodiscard]] bool bubble_at(std::span<const std::byte> blob,
                             const Array& bubbles,
                             std::uint64_t index,
                             Bubble& output) noexcept;

[[nodiscard]] bool slice_state_at(std::span<const std::byte> blob,
                                  const Bubble& bubble,
                                  std::uint64_t index,
                                  SliceState& output) noexcept;

[[nodiscard]] bool slice_entry(std::span<const std::byte> blob, SliceEntry& output) noexcept;

[[nodiscard]] bool registry_objects(std::span<const std::byte> blob,
                                    std::size_t descriptorOffset,
                                    Array& output) noexcept;

[[nodiscard]] bool registry_object_at(std::span<const std::byte> blob,
                                      const Array& objects,
                                      std::uint64_t index,
                                      std::uint32_t& tag) noexcept;

[[nodiscard]] bool object_key(std::span<const std::byte> blob, std::uint32_t& key) noexcept;

[[nodiscard]] bool object_slots(std::span<const std::byte> blob, Array& output) noexcept;

[[nodiscard]] bool object_slot_at(std::span<const std::byte> blob,
                                  const Array& slots,
                                  std::uint64_t index,
                                  Slot& output) noexcept;

[[nodiscard]] bool object_bubbles(std::span<const std::byte> blob, Array& output) noexcept;

[[nodiscard]] bool object_bubble_at(std::span<const std::byte> blob,
                                    const Array& bubbles,
                                    std::uint64_t index,
                                    ObjectBubble& output) noexcept;

[[nodiscard]] bool object_placed_handle_at(std::span<const std::byte> blob,
                                           const ObjectBubble& bubble,
                                           std::uint64_t index,
                                           std::uint32_t& handle) noexcept;

} // namespace dawn::middleware::content::packages::tables
