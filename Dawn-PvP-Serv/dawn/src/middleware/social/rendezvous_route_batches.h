#pragma once
#include "rendezvous_batch.h"

namespace dawn::middleware::social::rendezvous_batch {
struct RouteGroup final {
    std::array<std::size_t,kCapacity> indices{};
    std::size_t count{};
};
struct RouteGroups final {
    std::array<RouteGroup,kCapacity> groups{};
    std::size_t count{};
};
// groups an already drained batch; routes in first-occurrence order, packets FIFO within each
// lobby/target regardless of connection ID
inline bool group_routes(std::span<const realtime::Request> packets,RouteGroups& output) noexcept {
    output={};
    if(packets.empty()||packets.size()>kCapacity)return false;
    for(const auto& p:packets)if(p.operation!=realtime::Operation::sendRendezvous
        ||!p.lobbyId||!p.targetPlatformId||p.payloadSize<4||p.payloadSize>kPacketCapacity)return false;
    RouteGroups staged{};std::array<bool,kCapacity> used{};
    for(std::size_t first=0;first<packets.size();++first) {
        if(used[first])continue;
        auto& group=staged.groups[staged.count++];std::size_t bytes=2;
        for(std::size_t i=first;i<packets.size();++i) {
            if(used[i]||packets[i].lobbyId!=packets[first].lobbyId
                ||packets[i].targetPlatformId!=packets[first].targetPlatformId)continue;
            // don't overtake an earlier packet on this route when it can't fit
            if(bytes+2+packets[i].payloadSize>realtime::kEventPayloadCapacity)break;
            bytes+=2+packets[i].payloadSize;group.indices[group.count++]=i;used[i]=true;
        }
    }
    output=staged;return true;
}
}
