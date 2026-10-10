#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <bit>

#include "../../definition.h"

namespace dawn::state::activity::entity_slots::transactions {
inline std::size_t count(const LeaseMask& mask) noexcept {
    std::size_t result=0;
    for (const auto value:mask) result+=static_cast<std::size_t>(std::popcount(std::to_integer<unsigned char>(value)));
    return result;
}

inline bool empty(const LeaseMask& mask) noexcept {
    for (const std::byte value : mask) {
        if (value != std::byte{}) {
            return false;
        }
    }
    return true;
}

inline LeaseMask select_free(const LeaseMask& held, std::size_t requestedCount) noexcept {
    LeaseMask selected{};
    const std::size_t wanted = (std::min)(requestedCount, kSlotCount);
    std::size_t count = 0;
    for (std::size_t slot = 0; slot < kSlotCount && count < wanted; ++slot) {
        const std::size_t byteIndex = slot / kSlotsPerByte;
        const std::size_t bitIndex = slot % kSlotsPerByte;
        const std::byte bit{static_cast<unsigned char>(1U << bitIndex)};
        if ((held[byteIndex] & bit) != std::byte{}) {
            continue;
        }
        selected[byteIndex] |= bit;
        ++count;
    }
    return selected;
}

inline LeaseMask unite(const LeaseMask& first, const LeaseMask& second) noexcept {
    LeaseMask result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = first[index] | second[index];
    }
    return result;
}

// the client is granted low indices, so reserving the high end keeps one contiguous split
// and leaves the client's ascending selection unchanged
inline LeaseMask reserve_high(std::size_t count) noexcept {
    LeaseMask reserved{};
    const std::size_t wanted = (std::min)(count, kSlotCount);
    for (std::size_t index = 0; index < wanted; ++index) {
        const std::size_t slot = kSlotCount - 1 - index;
        reserved[slot / kSlotsPerByte] |=
            std::byte{static_cast<unsigned char>(1U << (slot % kSlotsPerByte))};
    }
    return reserved;
}

inline LeaseMask intersect(const LeaseMask& first, const LeaseMask& second) noexcept {
    LeaseMask result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = first[index] & second[index];
    }
    return result;
}

inline bool equal(const LeaseMask& first, const LeaseMask& second) noexcept {
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

inline bool exceeds(const LeaseMask& selected, const LeaseMask& available) noexcept {
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if ((selected[index] & ~available[index]) != std::byte{}) {
            return true;
        }
    }
    return false;
}

} // namespace dawn::state::activity::entity_slots::transactions
