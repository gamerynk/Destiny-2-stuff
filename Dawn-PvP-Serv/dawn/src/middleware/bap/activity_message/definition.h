#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include "../../../core/protocol/activity_peer_connection.h"
#include "request_provenance.h"

namespace dawn::middleware::bap::activity_message {

// only discriminator 1 carries the join envelope; variant 2 is rejected
inline constexpr std::byte kTransportDiscriminator{1};
inline constexpr std::size_t kMaximumPayloadSize = 0x7D800;
inline constexpr std::uint64_t kAbsentSessionId = 0;

struct Request final {
    std::uint64_t accountHandle{};
    std::uint32_t messageType{};
    std::uint32_t peerHeardMask{};
    // sensitive; never keep, log, capture, cache or save this view
    std::span<const std::byte> payload{};
    RequestProvenance provenance{};
};

struct JoinRequest final {
    core::protocol::ActivityPeerConnection peerConnection{};
    std::uint32_t correlation{};
    std::uint64_t sessionId{};
    std::uint64_t memberKey{};
    // the roster's participation key must be this character: the client binds the player by
    // matching it, so another character of the same account binds nothing
    std::uint64_t characterSoid{};
};

} // namespace dawn::middleware::bap::activity_message
