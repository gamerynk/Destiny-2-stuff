#pragma once

#include <cstddef>
#include <cstdint>

#include "memory.h"

namespace dawn::client::content::items::details::relative {

[[nodiscard]] inline bool resolve_block(const investment::Source& source,
                                        std::uintptr_t definition,
                                        std::size_t memberOffset,
                                        bool& present,
                                        std::uintptr_t& block) noexcept {
    present = false;
    block = 0;
    std::int64_t displacement = 0;
    std::uintptr_t memberAddress = 0;
    if (!memory::member_address(definition, memberOffset, memberAddress)
        || !memory::read_value(source, memberAddress, displacement)) {
        return false;
    }
    if (displacement == 0) {
        return true;
    }
    present = true;
    return memory::add_signed_fits(memberAddress, displacement, block);
}

} // namespace dawn::client::content::items::details::relative
