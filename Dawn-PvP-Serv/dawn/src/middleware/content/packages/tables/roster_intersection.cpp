#include "roster_intersection.h"

namespace dawn::middleware::content::packages::tables {
namespace {

constexpr std::uint32_t kSliceSetIndexBound = 512;
// omega's opening vignette/gate group and its global directive/dialogue sensor group
constexpr std::uint32_t kOmegaOpeningObject = 0x80F47BB2;
constexpr std::uint32_t kOmegaSensorObject = 0x80F47BDD;

[[nodiscard]] bool slice_set_bit(std::uint32_t sliceSetIndex, std::uint32_t& bit) noexcept {
    bit = 0;
    if (sliceSetIndex >= kSliceSetIndexBound || sliceSetIndex % kSliceSetIndexFactor != 0) {
        return false;
    }
    bit = sliceSetIndex / kSliceSetIndexFactor;
    return true;
}

} // namespace

bool carries_roster_slot(std::span<const std::byte> object, std::uint32_t objectTag) noexcept {
    Array slots{};
    if (!object_slots(object, slots)) {
        return false;
    }
    for (std::uint64_t index = 0; index < slots.count; ++index) {
        Slot slot{};
        if (!object_slot_at(object, slots, index, slot)) {
            // skip the slot, not the object: pvp_shaft's 0x80B921C2 has 282 slots and the only
            // qualifying type is at index 77; returning here lost all of them with no trace
            continue;
        }
        for (const std::uint16_t wanted : kRosterSlotTypes) {
            if (slot.type == wanted) {
                return true;
            }
        }
    }
    // verified mission_scot objects with authored slot types that aren't generic roster types
    return objectTag == kOmegaOpeningObject || objectTag == kOmegaSensorObject;
}

bool observe_slice_set(RosterIntersection& state, std::uint32_t sliceSetIndex) noexcept {
    std::uint32_t bit = 0;
    if (!slice_set_bit(sliceSetIndex, bit)) {
        state.overflowed = true;
        return false;
    }
    state.observedSets |= std::uint64_t{1} << bit;
    return true;
}

bool observe_roster_key(RosterIntersection& state,
                        std::uint32_t sliceSetIndex,
                        std::uint32_t objectKey) noexcept {
    std::uint32_t bit = 0;
    if (!slice_set_bit(sliceSetIndex, bit)) {
        state.overflowed = true;
        return false;
    }
    const std::uint64_t mask = std::uint64_t{1} << bit;
    state.observedSets |= mask;

    for (std::size_t index = 0; index < state.keyCount; ++index) {
        if (state.keys[index] == objectKey) {
            state.masks[index] |= mask;
            return true;
        }
    }
    if (state.keyCount == kRosterKeyCapacity) {
        // a dropped key would read as absent from every slice set, so the result is unusable
        state.overflowed = true;
        return false;
    }
    state.keys[state.keyCount] = objectKey;
    state.masks[state.keyCount] = mask;
    ++state.keyCount;
    return true;
}

void observe_unresolved_slice_set(RosterIntersection& state) noexcept {
    state.unresolvedSet = true;
}

bool safe_roster_keys(const RosterIntersection& state,
                      std::span<std::uint32_t> output,
                      std::size_t& count) noexcept {
    count = 0;
    if (state.overflowed) {
        return false;
    }
    // an unread slice set proves no key, so the intersection is empty
    if (state.unresolvedSet) {
        return true;
    }
    for (std::size_t index = 0; index < state.keyCount; ++index) {
        if (state.masks[index] != state.observedSets) {
            continue;
        }
        if (count == output.size()) {
            count = 0;
            return false;
        }
        output[count] = state.keys[index];
        ++count;
    }
    return true;
}

bool partial_roster_keys(const RosterIntersection& state,
                         std::span<std::uint32_t> keys,
                         std::span<std::uint64_t> masks,
                         std::size_t& count) noexcept {
    count = 0;
    if (state.overflowed || keys.size() != masks.size()) {
        return false;
    }
    // an unread slice set has no usable index, so no bubble-local key can be proved safe
    if (state.unresolvedSet) {
        return true;
    }
    for (std::size_t index = 0; index < state.keyCount; ++index) {
        const std::uint64_t mask = state.masks[index];
        if (mask == 0 || mask == state.observedSets) {
            continue;
        }
        if (count == keys.size()) {
            count = 0;
            return false;
        }
        keys[count] = state.keys[index];
        masks[count] = mask;
        ++count;
    }
    return true;
}

} // namespace dawn::middleware::content::packages::tables
