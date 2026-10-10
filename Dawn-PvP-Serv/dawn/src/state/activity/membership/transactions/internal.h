#pragma once

#include <cstddef>
#include <cstdint>

#include "../activity_membership_query.h"

namespace dawn::state::activity::membership::transactions {

inline constexpr std::uint8_t kRefreshGuardHalfWidth = 32;

inline std::uint64_t refresh_guard(std::uint32_t revision, std::int32_t bubbleIndex) noexcept {
    return (static_cast<std::uint64_t>(revision) << kRefreshGuardHalfWidth)
           | static_cast<std::uint32_t>(bubbleIndex);
}

inline bool equal(const Identity& first, const Identity& second) noexcept {
    return first.memberKey == second.memberKey && first.smallOpaque == second.smallOpaque
           && first.signedOpaque == second.signedOpaque && first.joinIdentity == second.joinIdentity
           && first.accountSoid == second.accountSoid && first.opaqueSoid == second.opaqueSoid
           && first.secondaryOpaque == second.secondaryOpaque;
}

inline bool equal(const SpawnState& first, const SpawnState& second) noexcept {
    return first.state == second.state && first.opaqueByte == second.opaqueByte
           && first.opaqueValue == second.opaqueValue;
}

inline bool equal(const TeleportState& first, const TeleportState& second) noexcept {
    return first.state == second.state && first.token == second.token
           && first.sliceSetIndex == second.sliceSetIndex
           && first.sliceSetHash == second.sliceSetHash;
}

inline bool equal(const AuthoritativeUpdate& first, const AuthoritativeUpdate& second) noexcept {
    return first.peerConnection==second.peerConnection
           && equal(first.spawn, second.spawn) && equal(first.teleport, second.teleport)
           && first.transitionToken == second.transitionToken
           && first.hasTransitionToken == second.hasTransitionToken
           && first.hasSpawn == second.hasSpawn && first.hasTeleport == second.hasTeleport;
}

inline bool equal(const Snapshot& first, const Snapshot& second) noexcept {
    for (std::size_t i=0; i<first.reservations.size(); ++i) {
        if (!equal(first.reservations[i],second.reservations[i])) return false;
    }
    return first.peerConnection==second.peerConnection
           && equal(first.identity, second.identity) && equal(first.spawn, second.spawn)
           && equal(first.teleport, second.teleport) && first.revision == second.revision
           && first.epoch == second.epoch && first.transitionToken == second.transitionToken;
}

inline MembershipState merge(const MembershipState& state,
                             const AuthoritativeUpdate& update) noexcept {
    MembershipState merged = state;
    merged.peerConnection=core::protocol::merge_peer_connection(state.peerConnection,update.peerConnection);
    if (update.hasTransitionToken) {
        merged.transitionToken = update.transitionToken;
        merged.hasTransitionToken = true;
    }
    if (update.hasSpawn) {
        merged.spawn = update.spawn;
    }
    if (update.hasTeleport) {
        merged.teleport = update.teleport;
    }
    // a negative region is the client's unset value on its way out, not a position
    if (update.hasRegion && update.region.index >= kAbsentRegionIndex + 1) {
        merged.region = update.region;
    }
    return merged;
}

inline bool moves_region(const MembershipState& state, const MembershipState& merged) noexcept {
    // the index alone names the bubble; a hash-only delta would rebuild the roster for nothing
    return state.region.index != merged.region.index;
}

inline bool moves_transition_token(const MembershipState& state,
                                   const MembershipState& merged) noexcept {
    return state.transitionToken != merged.transitionToken
           || state.hasTransitionToken != merged.hasTransitionToken;
}

inline bool equal_authoritative(const MembershipState& first,
                                const MembershipState& second) noexcept {
    return first.peerConnection==second.peerConnection
           && first.transitionToken == second.transitionToken
           && first.hasTransitionToken == second.hasTransitionToken
           && equal(first.spawn, second.spawn) && equal(first.teleport, second.teleport);
}

inline bool valid_identity(const Identity& identity, std::uint64_t memberKey) noexcept {
    return identity.memberKey == memberKey;
}

inline Snapshot make_snapshot(const MembershipState& state,
                              const Identity& identity,
                              std::uint32_t revision) noexcept {
    Snapshot snapshot{};
    snapshot.peerConnection=state.peerConnection;
    snapshot.identity = identity;
    snapshot.reservations = state.reservations;
    snapshot.spawn = state.spawn;
    snapshot.teleport = state.teleport;
    snapshot.revision = revision;
    snapshot.epoch = kStableEpoch;
    snapshot.transitionToken =
        state.hasTransitionToken ? state.transitionToken : kInitialTransitionToken;
    return snapshot;
}

[[nodiscard]] const SessionRecord* prepare_base(const ActivityState& state,
                                                std::uint64_t primarySoid,
                                                std::uint64_t sessionId,
                                                PendingMutation& mutation) noexcept;

void publish_change(ActivityState& state, SessionRecord& record) noexcept;

[[nodiscard]] bool commit_authoritative(ActivityState& state,
                                        SessionRecord& record,
                                        const PendingMutation& prepared) noexcept;

} // namespace dawn::state::activity::membership::transactions
