#include "scenario_reader.h"

#include "internal.h"

namespace dawn::middleware::content::packages::tables {
namespace {

constexpr std::size_t kStateEnabledOffset = 0;
constexpr std::size_t kStateHashOffset = 4;
constexpr std::size_t kStateBubbleHashOffset = 64;
constexpr std::size_t kStateEntryTagOffset = 68;
// client getter returns array header + 28 and data starts 16 after the header, so byte 12; only 0 and 1 seen
constexpr std::size_t kStatePublicOffset = 12;

} // namespace

bool scenario_bubbles(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    return find_array_at(blob, kScenarioBubbleDescriptor, output);
}

bool bubble_at(std::span<const std::byte> blob,
               const Array& bubbles,
               std::uint64_t index,
               Bubble& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (!element_offset(bubbles.dataOffset, bubbles.count, kBubbleStride, index, offset)
        || !read(blob, offset, output.nameHash)) {
        return false;
    }
    // a bubble with no states is ordinary, not a failure
    Array states{};
    if (find_array_at(blob, offset + kBubbleStateDescriptor, states)) {
        output.stateCount = states.count;
        output.stateDataOffset = states.dataOffset;
    }
    return true;
}

bool slice_state_at(std::span<const std::byte> blob,
                    const Bubble& bubble,
                    std::uint64_t index,
                    SliceState& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (!element_offset(
            bubble.stateDataOffset, bubble.stateCount, kSliceStateStride, index, offset)) {
        return false;
    }
    std::uint8_t enabled = 0;
    std::uint8_t isPublic = 0;
    if (!read(blob, offset + kStateEnabledOffset, enabled)
        || !read(blob, offset + kStateHashOffset, output.stateHash)
        || !read(blob, offset + kStatePublicOffset, isPublic)
        || !read(blob, offset + kStateMapBubbleIndexOffset, output.mapBubbleIndex)
        || !read(blob, offset + kStateBubbleHashOffset, output.bubbleNameHash)
        || !read(blob, offset + kStateEntryTagOffset, output.entryTag)) {
        output = {};
        return false;
    }
    output.enabled = enabled != 0;
    output.isPublic = isPublic != 0;
    return true;
}

bool slice_entry(std::span<const std::byte> blob, SliceEntry& output) noexcept {
    output = {};
    if (!read(blob, kSliceEntryIndexOffset, output.index)
        || !read(blob, kSliceEntryRegistryOffset, output.registryTag)) {
        output = {};
        return false;
    }
    return true;
}

bool registry_objects(std::span<const std::byte> blob,
                      std::size_t descriptorOffset,
                      Array& output) noexcept {
    output = {};
    return find_array_at(blob, descriptorOffset, output);
}

bool registry_object_at(std::span<const std::byte> blob,
                        const Array& objects,
                        std::uint64_t index,
                        std::uint32_t& tag) noexcept {
    tag = 0;
    std::size_t offset = 0;
    return element_offset(objects.dataOffset, objects.count, kRegistryElementStride, index, offset)
           && read(blob, offset, tag);
}

bool object_key(std::span<const std::byte> blob, std::uint32_t& key) noexcept {
    key = 0;
    return read(blob, kObjectKeyOffset, key);
}

bool object_slots(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    return find_array_at(blob, kObjectSlotDescriptor, output);
}

bool object_slot_at(std::span<const std::byte> blob,
                    const Array& slots,
                    std::uint64_t index,
                    Slot& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (!element_offset(slots.dataOffset, slots.count, kSlotStride, index, offset)
        || !read(blob, offset, output.type)
        || !read(blob, offset + sizeof output.type, output.nameHash)) {
        output = {};
        return false;
    }
    return true;
}

bool object_bubbles(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    return find_array_at(blob, kObjectBubbleDescriptor, output);
}

bool object_bubble_at(std::span<const std::byte> blob,
                      const Array& bubbles,
                      std::uint64_t index,
                      ObjectBubble& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (!element_offset(bubbles.dataOffset, bubbles.count, kObjectBubbleStride, index, offset)
        || !read(blob, offset, output.bubbleIndex)) {
        output = {};
        return false;
    }
    Array handles{};
    if (find_array_at(blob, offset + kObjectBubbleHandleDescriptor, handles)) {
        output.handleCount = handles.count;
        output.handleDataOffset = handles.dataOffset;
    }
    return true;
}

bool object_placed_handle_at(std::span<const std::byte> blob,
                             const ObjectBubble& bubble,
                             std::uint64_t index,
                             std::uint32_t& handle) noexcept {
    handle = 0;
    std::size_t offset = 0;
    return element_offset(bubble.handleDataOffset,
                          bubble.handleCount,
                          kObjectPlacedHandleStride,
                          index,
                          offset)
           && read(blob, offset, handle);
}

} // namespace dawn::middleware::content::packages::tables
