#pragma once
#include "realtime_codec.h"
#include <algorithm>

namespace dawn::middleware::social::rendezvous_batch {
// transport envelope only; every native connection-id + signalling packet stays opaque. fits twelve
// small or seven maximum-size packets; four response events give at most 48 native callbacks (ring holds 64)
inline constexpr std::size_t kCapacity=12;
inline constexpr std::size_t kPacketCapacity=4+0x200;
struct View {
    std::array<std::span<const std::byte>,kCapacity> packets{};
    std::size_t count{};
};
inline bool decode(std::span<const std::byte> input,View& output) noexcept {
    output={};
    if(input.size()<2 || input[0]!=std::byte{1})return false;
    const auto count=std::to_integer<std::size_t>(input[1]);
    if(count==0 || count>kCapacity)return false;
    View parsed{};std::size_t offset=2;
    for(std::size_t i=0;i<count;++i) {
        if(input.size()-offset<2)return false;
        const auto length=std::to_integer<std::size_t>(input[offset])
            | (std::to_integer<std::size_t>(input[offset+1])<<8);
        offset+=2;
        if(length<4 || length>kPacketCapacity || length>input.size()-offset)return false;
        parsed.packets[i]=input.subspan(offset,length);offset+=length;
    }
    if(offset!=input.size())return false;
    parsed.count=count;output=parsed;return true;
}
inline bool encode(std::span<const realtime::Request> packets,realtime::Request& output) noexcept {
    output={};
    if(packets.empty() || packets.size()>kCapacity)return false;
    const auto lobby=packets.front().lobbyId,target=packets.front().targetPlatformId;
    if(lobby==0 || target==0)return false;
    std::size_t total=2;
    for(const auto& packet:packets) {
        if(packet.operation!=realtime::Operation::sendRendezvous || packet.lobbyId!=lobby
            || packet.targetPlatformId!=target || packet.payloadSize<4
            || packet.payloadSize>kPacketCapacity)return false;
        total+=2+packet.payloadSize;
    }
    if(total>output.payload.size())return false;
    output.operation=realtime::Operation::sendRendezvousBatch;
    output.lobbyId=lobby;output.targetPlatformId=target;
    output.payload[0]=std::byte{1};output.payload[1]=static_cast<std::byte>(packets.size());
    std::size_t offset=2;
    for(const auto& packet:packets) {
        output.payload[offset++]=static_cast<std::byte>(packet.payloadSize&0xFF);
        output.payload[offset++]=static_cast<std::byte>(packet.payloadSize>>8);
        std::copy_n(packet.payload.begin(),packet.payloadSize,output.payload.begin()+offset);
        offset+=packet.payloadSize;
    }
    output.payloadSize=offset;return true;
}
} // namespace dawn::middleware::social::rendezvous_batch
