#pragma once

#include <cstddef>

#include "../definition.h"

namespace dawn::state::activity::transactions {

inline std::size_t find_session(const ActivityState& state, std::uint64_t sessionId) noexcept {
    for (std::size_t index = 0; index < state.sessions.size(); ++index) {
        const SessionRecord& record = state.sessions[index];
        if (record.occupied && record.sessionId == sessionId) {
            return index;
        }
    }
    return kInvalidSessionSlot;
}

inline std::size_t select_target(const ActivityState& state) noexcept {
    for (std::size_t index = 0; index < state.sessions.size(); ++index) {
        if (!state.sessions[index].occupied) {
            return index;
        }
    }

    std::size_t selected{};
    for (std::size_t index = 1; index < state.sessions.size(); ++index) {
        if (state.sessions[index].createdRevision < state.sessions[selected].createdRevision) {
            selected = index;
        }
    }
    return selected;
}

// shared imports may reclaim only previous-runtime records that have not rejoined
// caller holds the root State lock; commit repeats the selection with revision guards
inline std::size_t select_shared_target(const ActivityState& state,
                                        std::uint64_t accountSoid) noexcept {
    for (std::size_t i = 0; i < state.sessions.size(); ++i)
        if (!state.sessions[i].occupied) return i;
    std::size_t target = kInvalidSessionSlot;
    for (std::size_t i = 0; i < state.sessions.size(); ++i) {
        const auto& row = state.sessions[i];
        if (!row.restoredDormant || row.ownerAccountSoid != accountSoid
            || row.sharedOwnerAccountSoid != 0) continue;
        if (target == kInvalidSessionSlot
            || row.createdRevision < state.sessions[target].createdRevision) target = i;
    }
    return target;
}

inline constexpr std::uint64_t kSessionClass = 0x00200000ULL;

// a bare counter matches no client record and sim event 20 drops the roster silently
// built form is 0x9EAA000100200001
[[nodiscard]] inline std::uint64_t compose_session_soid(std::uint64_t soidBase,
                                                        std::uint64_t counter) noexcept {
    return soidBase != 0 ? soidBase | kSessionClass | counter : counter;
}

inline bool allocation_available(const ActivityState& state) noexcept {
    return !state.allocatorExhausted && state.nextSessionId != kAbsentSessionId
           && state.stateRevision != kMaximumRevision
           && state.allocatorRevision != kMaximumRevision;
}

inline void advance_allocator(ActivityState& state) noexcept {
    if (state.nextSessionId == kMaximumSessionId) {
        state.allocatorExhausted = true;
    } else {
        ++state.nextSessionId;
    }
    ++state.allocatorRevision;
    if (state.allocatorRevision == kMaximumRevision) {
        state.allocatorExhausted = true;
    }
}

} // namespace dawn::state::activity::transactions
