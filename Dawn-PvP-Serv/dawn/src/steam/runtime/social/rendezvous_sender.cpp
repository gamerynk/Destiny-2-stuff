#include "rendezvous_sender.h"
#include "bounded_batch_sender.h"
#include "realtime_client.h"
#include "../../../middleware/social/rendezvous_route_batches.h"
#include "../../../core/logging/log.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace dawn::steam::runtime::social::rendezvous_sender {
namespace {
namespace wire=middleware::social::realtime;
namespace envelope=middleware::social::rendezvous_batch;
BoundedBatchSender<wire::Request,64,envelope::kCapacity> g_sender;

void send(std::span<const wire::Request> packets) noexcept {
    envelope::RouteGroups routes{};
    if(!envelope::group_routes(packets,routes))return;
    std::array<wire::Request,envelope::kCapacity> ordered{};
    for(std::size_t route=0;route<routes.count;++route) {
        const auto& selected=routes.groups[route];const auto count=selected.count;
        for(std::size_t i=0;i<count;++i)ordered[i]=packets[selected.indices[i]];
        const auto group=std::span(ordered).first(count);const auto& first=group.front();
        // receiver permission is still checked by the server; only the lobby generation guard is here
        if(realtime_client::lobby_snapshot().id!=first.lobbyId) {
            core::log::write(core::log::Channel::client,core::log::Level::warn,
                "ev=rendezvous_sender result=discarded reason=lobby_changed_or_peer_left");
            continue;
        }
        wire::Request request{};
        if(count==1)request=first;
        else if(!envelope::encode(group,request))continue;
        wire::Response response{};const auto started=GetTickCount64();
        const bool executed=realtime_client::execute(request,response);
        const auto duration=GetTickCount64()-started;
        for(const auto& packet:group) {
            std::uint32_t connection{};std::memcpy(&connection,packet.payload.data(),sizeof(connection));
            std::array<char,384> line{};
            const int length=std::snprintf(line.data(),line.size(),
                "ev=rendezvous_sender result=%s remote=0x%016llX lobby=0x%016llX"
                " source_connection=%lu bytes=%zu wire_result=%d transport_ms=%llu batch=%zu",
                executed?"executed":"transport_fail",static_cast<unsigned long long>(packet.targetPlatformId),
                static_cast<unsigned long long>(packet.lobbyId),static_cast<unsigned long>(connection),
                packet.payloadSize-sizeof(connection),response.result,static_cast<unsigned long long>(duration),count);
            if(length>0)core::log::write(core::log::Channel::client,
                executed && response.result==1?core::log::Level::info:core::log::Level::warn,
                {line.data(),(std::min)(static_cast<std::size_t>(length),line.size()-1)});
        }
    }
}
}
bool start() noexcept {return g_sender.start(&send);}
void shutdown() noexcept {g_sender.stop();}
bool enqueue(const wire::Request& request) noexcept {
    if(request.operation!=wire::Operation::sendRendezvous || request.lobbyId==0
        || request.targetPlatformId==0 || request.payloadSize<sizeof(std::uint32_t)
        || request.payloadSize>envelope::kPacketCapacity)return false;
    return g_sender.enqueue(request);
}
}
