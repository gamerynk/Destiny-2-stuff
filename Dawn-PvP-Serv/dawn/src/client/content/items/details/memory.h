#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "../../investment/source.h"
#include "../layout.h"

namespace dawn::client::content::items::details::memory {

[[nodiscard]] inline bool
add_fits(std::uintptr_t left, std::uintptr_t right, std::uintptr_t& result) noexcept {
    if (right > (std::numeric_limits<std::uintptr_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] inline bool
add_signed_fits(std::uintptr_t base, std::int64_t relative, std::uintptr_t& result) noexcept {
    if (relative >= 0) {
        const auto positive = static_cast<std::uint64_t>(relative);
        if (positive > (std::numeric_limits<std::uintptr_t>::max)()) {
            return false;
        }
        return add_fits(base, static_cast<std::uintptr_t>(positive), result);
    }

    const std::uint64_t magnitude = static_cast<std::uint64_t>(-(relative + 1)) + 1U;
    if (magnitude > (std::numeric_limits<std::uintptr_t>::max)()
        || static_cast<std::uintptr_t>(magnitude) > base) {
        return false;
    }
    result = base - static_cast<std::uintptr_t>(magnitude);
    return true;
}

[[nodiscard]] inline bool
multiply_fits(std::uintptr_t left, std::uintptr_t right, std::uintptr_t& result) noexcept {
    if (left != 0 && right > (std::numeric_limits<std::uintptr_t>::max)() / left) {
        return false;
    }
    result = left * right;
    return true;
}

template <typename Value>
[[nodiscard]] bool
read_value(const investment::Source& source, std::uintptr_t address, Value& value) noexcept {
    return source.handles.read != nullptr
           && source.handles.read(source.handles.context,
                                  address,
                                  std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

[[nodiscard]] inline bool
member_address(std::uintptr_t base, std::size_t offset, std::uintptr_t& address) noexcept {
    return add_fits(base, static_cast<std::uintptr_t>(offset), address);
}

template <typename Value>
[[nodiscard]] bool read_member(const investment::Source& source,
                               std::uintptr_t base,
                               std::size_t offset,
                               Value& value) noexcept {
    std::uintptr_t address = 0;
    return member_address(base, offset, address) && read_value(source, address, value);
}

[[nodiscard]] inline bool resolve_member(const investment::Source& source,
                                         std::uintptr_t parent,
                                         std::size_t offset,
                                         std::uintptr_t& child) noexcept {
    std::uint32_t handle = 0;
    return read_member(source, parent, offset, handle)
           && handles::resolve(source.handles, handle, child);
}

} // namespace dawn::client::content::items::details::memory
