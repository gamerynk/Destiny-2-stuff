#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace dawn::middleware::content::packages::tables {

template <typename Value>
[[nodiscard]] inline bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    // subtract instead of add so a large offset can't wrap past the size
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

[[nodiscard]] inline bool element_offset(std::size_t dataOffset,
                                         std::uint64_t count,
                                         std::size_t stride,
                                         std::uint64_t index,
                                         std::size_t& offset) noexcept {
    if (index >= count) {
        return false;
    }
    offset = dataOffset + static_cast<std::size_t>(index) * stride;
    return true;
}

} // namespace dawn::middleware::content::packages::tables
