#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "activity_player_team.h"

namespace dawn::core::protocol {
struct ActivityPeerConnection final {
    std::uint8_t flags{};
    std::array<std::byte,86> proxy{}, peer{}, alternate{};
    bool hasFlags{}, hasProxy{}, hasPeer{}, hasAlternate{};
    ActivityPlayerTeam playerTeam{};
    bool operator==(const ActivityPeerConnection&) const = default;
};
inline ActivityPeerConnection merge_peer_connection(ActivityPeerConnection value,
    const ActivityPeerConnection& delta) noexcept {
    if(delta.hasFlags){value.flags=delta.flags;value.hasFlags=true;}
    if(delta.hasProxy){value.proxy=delta.proxy;value.hasProxy=true;}
    if(delta.hasPeer){value.peer=delta.peer;value.hasPeer=true;}
    if(delta.hasAlternate){value.alternate=delta.alternate;value.hasAlternate=true;}
    value.playerTeam=merge_player_team(value.playerTeam,delta.playerTeam);
    return value;
}
}
