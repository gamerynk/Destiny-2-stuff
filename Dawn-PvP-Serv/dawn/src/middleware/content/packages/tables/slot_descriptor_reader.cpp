#include "slot_descriptor_reader.h"

#include "definition_index_table.h"
#include "internal.h"

namespace dawn::middleware::content::packages::tables {
namespace {

[[nodiscard]] bool descriptor_at(std::span<const std::byte> blob,
                                 std::size_t base,
                                 std::uint32_t ownTag,
                                 std::uint32_t registryKey,
                                 SlotDescriptor& output) noexcept {
    output = {};
    std::uint32_t tag = 0;
    std::uint32_t mark = 0;
    std::uint32_t key = 0;
    if (!read(blob, base + kDescriptorOwnTagOffset, tag) || tag != ownTag
        || !read(blob, base + kDescriptorMarkOffset, mark) || mark != kDescriptorMark
        || !read(blob, base + kDescriptorRegistryKeyOffset, key) || key != registryKey) {
        return false;
    }
    SlotDescriptor candidate{};
    if (!read(blob, base + kDescriptorComponentClassOffset, candidate.componentClass)
        || !read(blob, base + kDescriptorSenseSchemaOffset, candidate.senseSchema)
        || !read(blob, base + kDescriptorAuthSchemaOffset, candidate.authSchema)
        || !read(blob, base + kDescriptorSlotTypeOffset, candidate.slotType)
        || !read(blob, base + kDescriptorSlotIndexOffset, candidate.slotIndex)) {
        return false;
    }
    // slot types with no auth or sense schema record the absent sentinel, so requiring a class
    // in both fields drops every such descriptor
    if (!is_class_id(candidate.componentClass) || !is_schema_id(candidate.senseSchema)
        || !is_schema_id(candidate.authSchema)) {
        return false;
    }
    output = candidate;
    return true;
}

} // namespace

bool visit_slot_descriptors(std::span<const std::byte> blob,
                            std::uint32_t ownTag,
                            std::uint32_t registryKey,
                            DescriptorVisitor visitor,
                            void* context) noexcept {
    if (visitor == nullptr) {
        return false;
    }
    if (blob.size() < kDescriptorSize) {
        return true;
    }
    const std::size_t last = blob.size() - kDescriptorSize;
    for (std::size_t base = 0; base <= last; base += kDescriptorStep) {
        SlotDescriptor descriptor{};
        if (!descriptor_at(blob, base, ownTag, registryKey, descriptor)) {
            continue;
        }
        if (!visitor(context, descriptor)) {
            return false;
        }
    }
    return true;
}

bool descriptor_child_tags(std::span<const std::byte> blob,
                           std::uint32_t classId,
                           std::span<std::uint32_t> output,
                           std::size_t& count) noexcept {
    count = 0;
    if (classId == kSlotRedirectClass) {
        if (output.empty() || !read(blob, kSlotRedirectTagOffset, output[0])) {
            return false;
        }
        count = 1;
        return true;
    }
    if (classId != kSlotIndirectClass) {
        return false;
    }
    Array handles{};
    if (!find_array_at(blob, kSlotIndirectDescriptor, handles)
        || handles.count > output.size()) {
        return false;
    }
    for (std::uint64_t index = 0; index < handles.count; ++index) {
        const std::size_t offset =
            handles.dataOffset + static_cast<std::size_t>(index) * sizeof(std::uint32_t);
        if (!read(blob, offset, output[static_cast<std::size_t>(index)])) {
            count = 0;
            return false;
        }
    }
    count = static_cast<std::size_t>(handles.count);
    return true;
}

bool next_descriptor_tag(std::span<const std::byte> blob,
                         std::uint32_t classId,
                         std::uint32_t& tag) noexcept {
    tag = 0;
    if (classId == kSlotRedirectClass) {
        return read(blob, kSlotRedirectTagOffset, tag);
    }
    if (classId != kSlotIndirectClass) {
        return false;
    }
    Array handles{};
    return find_array_at(blob, kSlotIndirectDescriptor, handles) && handles.count != 0
           && read(blob, handles.dataOffset, tag);
}

} // namespace dawn::middleware::content::packages::tables
