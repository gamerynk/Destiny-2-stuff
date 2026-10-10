#include "component_container_reader.h"

#include <algorithm>

#include "internal.h"

namespace dawn::middleware::content::packages::tables {

bool component_resource(std::span<const std::byte> blob, std::uint32_t& tag) noexcept {
    tag = 0;
    return read(blob, kComponentResourceOffset, tag);
}

bool container_bubble_mask(std::span<const std::byte> blob,
                           std::span<std::uint8_t> output) noexcept {
    if (output.size() != kContainerBubbleMaskBytes) {
        return false;
    }
    std::fill(output.begin(), output.end(), std::uint8_t{});
    for (std::size_t index = 0; index < kContainerBubbleMaskBytes; ++index) {
        if (!read(blob, kContainerBubbleMaskOffset + index, output[index])) {
            std::fill(output.begin(), output.end(), std::uint8_t{});
            return false;
        }
    }
    return true;
}

bool container_members(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    return find_array_at(blob, kContainerMemberDescriptor, output);
}

bool container_member_at(std::span<const std::byte> blob,
                         const Array& members,
                         std::uint64_t index,
                         std::uint32_t& tag) noexcept {
    tag = 0;
    std::size_t offset = 0;
    return element_offset(members.dataOffset, members.count, kContainerMemberStride, index, offset)
           && read(blob, offset, tag);
}

bool bubble_in_mask(std::span<const std::uint8_t> mask, std::size_t index) noexcept {
    const std::size_t byte = index / 8;
    return byte < mask.size() && (mask[byte] >> (index % 8) & 1U) != 0;
}

} // namespace dawn::middleware::content::packages::tables
