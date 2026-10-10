#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "../definition.h"

namespace dawn::state::matchmaking::transactions {

inline constexpr std::size_t no_variant() noexcept {
    return kVariantCapacity;
}

inline ContextSlot* resolve(MatchmakingState& state, ContextHandle handle) noexcept {
    if (handle.generation == kInvalidGeneration || handle.slot >= state.contexts.size()) {
        return nullptr;
    }
    ContextSlot& slot = state.contexts[handle.slot];
    return slot.active && slot.generation == handle.generation ? &slot : nullptr;
}

inline std::size_t find_variant(const ContextData& data, std::uint64_t variant) noexcept {
    for (std::size_t index = 0; index < data.variants.size(); ++index) {
        const VariantRecord& record = data.variants[index];
        if (record.occupied && record.variant == variant) {
            return index;
        }
    }
    return no_variant();
}

inline std::size_t select_variant_slot(const ContextData& data) noexcept {
    for (std::size_t index = 0; index < data.variants.size(); ++index) {
        if (!data.variants[index].occupied) {
            return index;
        }
    }
    std::size_t selected = 0;
    for (std::size_t index = 1; index < data.variants.size(); ++index) {
        // strict compare makes equal revisions evict the lowest table index
        if (data.variants[index].lastUpdatedRevision
            < data.variants[selected].lastUpdatedRevision) {
            selected = index;
        }
    }
    return selected;
}

inline bool allocator_available(const MatchmakingState& state) noexcept {
    return !state.allocatorExhausted && state.nextAdvertisementId != kAbsentAdvertisementId
           && state.allocatorRevision != (std::numeric_limits<std::uint64_t>::max)();
}

inline void advance_allocator(MatchmakingState& state) noexcept {
    if (state.nextAdvertisementId == (std::numeric_limits<std::uint64_t>::max)()) {
        state.allocatorExhausted = true;
    } else {
        ++state.nextAdvertisementId;
    }
    ++state.allocatorRevision;
}

inline bool valid_descriptor(const PendingMutation& mutation) noexcept {
    if (mutation.hasDescriptor) {
        return mutation.descriptorData != nullptr && mutation.descriptorSize == kDescriptorSize;
    }
    return mutation.descriptorData == nullptr && mutation.descriptorSize == 0;
}

} // namespace dawn::state::matchmaking::transactions
