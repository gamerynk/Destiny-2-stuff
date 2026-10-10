#pragma once

#include <cstdint>
#include <array>
#include <limits>
#include "../../../core/protocol/activity_peer_connection.h"

namespace dawn::state::activity::membership {

inline constexpr std::uint32_t kAbsentRevision = 0;
inline constexpr std::uint32_t kInitialRevision = 1;
// stop before wrap so a stale acknowledgement cannot look current
inline constexpr std::uint32_t kMaximumMembershipRevision =
    (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::uint32_t kStableEpoch = 0;
inline constexpr std::uint8_t kInitialTransitionToken = 1;
inline constexpr std::int8_t kMinimumMirroredState = -1;
inline constexpr std::int8_t kMaximumMirroredState = 6;
inline constexpr std::int32_t kAbsentSliceSetIndex = -1;
inline constexpr std::int32_t kMaximumSliceSetIndex = 1022;
inline constexpr std::int32_t kMinimumRefreshBubble = -1;
inline constexpr std::int32_t kMaximumRefreshBubble = 63;
inline constexpr std::uint64_t kInvalidOpaqueSoid = (std::numeric_limits<std::uint64_t>::max)();
inline constexpr std::uint64_t kInvalidJoinIdentity = (std::numeric_limits<std::uint64_t>::max)();

struct Identity final {
    std::uint64_t memberKey{};
    std::int32_t smallOpaque{};
    std::int32_t signedOpaque{};
    std::uint64_t joinIdentity{};
    std::uint64_t accountSoid{};
    std::uint64_t opaqueSoid{};
    std::uint64_t secondaryOpaque{};
};

using Reservations = std::array<Identity, 31>;

struct SpawnState final {
    std::int8_t state{};
    std::uint8_t opaqueByte{};
    std::uint64_t opaqueValue{};
};

struct TeleportState final {
    std::int8_t state{};
    std::uint8_t token{};
    std::int32_t sliceSetIndex{kAbsentSliceSetIndex};
    std::uint32_t sliceSetHash{};
};

inline constexpr std::int32_t kAbsentRegionIndex = -1;
inline constexpr std::int32_t kMaximumRegionIndex = 1022;

struct RegionState final {
    std::int32_t index{kAbsentRegionIndex};
    std::uint32_t hash{};
};

struct AuthoritativeUpdate final {
    core::protocol::ActivityPeerConnection peerConnection{};
    SpawnState spawn{};
    TeleportState teleport{};
    RegionState region{};
    std::uint8_t transitionToken{};
    bool hasTransitionToken{};
    bool hasSpawn{};
    bool hasTeleport{};
    bool hasRegion{};
};

struct Snapshot final {
    core::protocol::ActivityPeerConnection peerConnection{};
    Identity identity{};
    Reservations reservations{};
    SpawnState spawn{};
    TeleportState teleport{};
    std::uint32_t revision{};
    std::uint32_t epoch{kStableEpoch};
    std::uint8_t transitionToken{};
};

struct MembershipState final {
    core::protocol::ActivityPeerConnection peerConnection{};
    Identity identity{};
    Reservations reservations{};
    SpawnState spawn{};
    TeleportState teleport{};
    RegionState region{};
    std::uint32_t revision{};
    std::uint32_t acknowledgedRevision{};
    std::uint8_t transitionToken{};
    bool hasTransitionToken{};
    bool hasIdentity{};
};

enum class MutationKind : std::uint8_t {
    none,
    identity,
    authoritative,
    refresh,
    acknowledgement,
    reservations,
};

} // namespace dawn::state::activity::membership
