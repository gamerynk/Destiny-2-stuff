#include "middleware/social/rendezvous_route_batches.h"
#include "steam/runtime/social/bounded_batch_sender.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace wire=dawn::middleware::social::realtime;
namespace batch=dawn::middleware::social::rendezvous_batch;
static HANDLE entered{},releaseFirst{},finished{};
static std::array<int,65> delivered{};
static std::atomic<unsigned> receivedCount{},calls{},largest{};
static void receive(std::span<const int> packets) noexcept {
    ++calls;
    if(receivedCount==0){SetEvent(entered);WaitForSingleObject(releaseFirst,5000);}
    if(packets.size()>largest)largest=static_cast<unsigned>(packets.size());
    for(const auto value:packets){const auto index=receivedCount.fetch_add(1);if(index<delivered.size())delivered[index]=value;}
    if(receivedCount==65)SetEvent(finished);
}
static bool check(bool ok,const char* name){if(!ok)std::printf("FAIL: rendezvous batch: %s\n",name);return ok;}
int main() {
    std::array<wire::Request,batch::kCapacity> packets{};
    for(std::size_t i=0;i<packets.size();++i) {
        auto& p=packets[i];p.operation=wire::Operation::sendRendezvous;p.lobbyId=7;p.targetPlatformId=9;
        p.payloadSize=4+5+i;p.payload.fill(static_cast<std::byte>(i));
        const auto connection=static_cast<std::uint32_t>(0xFF000000U+i);
        std::memcpy(p.payload.data(),&connection,4);
    }
    wire::Request envelope{};batch::View view{};
    if(!check(batch::encode(packets,envelope) && batch::decode(std::span(envelope.payload).first(envelope.payloadSize),view)
        && view.count==packets.size(),"full small-packet envelope"))return 1;
    for(std::size_t i=0;i<packets.size();++i)
        if(!check(view.packets[i].size()==packets[i].payloadSize
            && std::equal(view.packets[i].begin(),view.packets[i].end(),packets[i].payload.begin()),"byte-exact ordered roundtrip"))return 1;
    std::array<std::byte,wire::kMaximumResponseSize> transport{};std::size_t written{};wire::Request decoded{};
    if(!check(wire::encode_request(envelope,transport,written)
        && wire::decode_request(std::span(transport).first(written),decoded)
        && decoded.operation==wire::Operation::sendRendezvousBatch && decoded.payloadSize==envelope.payloadSize
        && decoded.payload==envelope.payload,"request codec admits exact new operation and payload"))return 1;
    wire::Response response{},received{};response.result=1;response.eventCount=1;response.cursor=1;
    response.events[0].sequence=1;response.events[0].kind=wire::EventKind::rendezvousBatch;
    response.events[0].sourcePlatformId=7;response.events[0].memberPlatformId=9;
    response.events[0].payload=envelope.payload;response.events[0].payloadSize=envelope.payloadSize;
    if(!check(wire::encode_response(response,transport,written)
        && wire::decode_response(std::span(transport).first(written),received)
        && received.eventCount==1 && received.events[0].kind==wire::EventKind::rendezvousBatch
        && received.events[0].payload==envelope.payload,"recipient response codec preserves new event"))return 1;
    decoded.operation=static_cast<wire::Operation>(23);
    if(!check(wire::encode_request(decoded,transport,written)
        && !wire::decode_request(std::span(transport).first(written),decoded),"unknown operation rejected"))return 1;
    for(std::size_t size=0;size<envelope.payloadSize;++size)
        if(!check(!batch::decode(std::span(envelope.payload).first(size),view) && view.count==0,"truncation is atomic"))return 1;
    auto bad=envelope;bad.payload[0]=std::byte{2};
    if(!check(!batch::decode(std::span(bad.payload).first(bad.payloadSize),view),"unknown version"))return 1;
    bad=envelope;bad.payload[1]=std::byte{0};
    if(!check(!batch::decode(std::span(bad.payload).first(bad.payloadSize),view),"empty batch"))return 1;
    bad=envelope;bad.payload[2]=bad.payload[3]=std::byte{0xFF};
    if(!check(!batch::decode(std::span(bad.payload).first(bad.payloadSize),view),"oversized native packet"))return 1;
    if(!check(!batch::decode(std::span(envelope.payload).first(envelope.payloadSize+1),view),"trailing bytes"))return 1;
    packets[1].targetPlatformId=10;
    if(!check(!batch::encode(packets,envelope) && envelope.payloadSize==0,"mixed targets rejected"))return 1;
    packets[1].targetPlatformId=9;packets[1].lobbyId=8;
    if(!check(!batch::encode(packets,envelope),"mixed sender generations rejected"))return 1;
    packets[1].lobbyId=7;
    for(auto& p:packets)p.payloadSize=batch::kPacketCapacity;
    if(!check(batch::encode(std::span(packets).first(7),envelope)
        && !batch::encode(std::span(packets).first(8),envelope),"existing event byte budget enforced"))return 1;
    for(std::size_t i=0;i<packets.size();++i){packets[i].lobbyId=7;packets[i].targetPlatformId=9+i%2;packets[i].payloadSize=9;}
    batch::RouteGroups routes{};
    if(!check(batch::group_routes(packets,routes)&&routes.count==2
        &&routes.groups[0].count==6&&routes.groups[1].count==6,"alternating peers need two transports"))return 1;
    std::array<wire::Request,batch::kCapacity> ordered{};
    for(std::size_t r=0;r<routes.count;++r){
        const auto& g=routes.groups[r];
        for(std::size_t i=0;i<g.count;++i){
            if(!check(g.indices[i]==r+2*i,"stable route FIFO across connections"))return 1;
            ordered[i]=packets[g.indices[i]];
        }
        if(!check(batch::encode(std::span(ordered).first(g.count),envelope)
            &&batch::decode(std::span(envelope.payload).first(envelope.payloadSize),view),"group envelope decodes"))return 1;
        for(std::size_t i=0;i<g.count;++i)if(!check(std::equal(view.packets[i].begin(),view.packets[i].end(),ordered[i].payload.begin()),"opaque grouped bytes unchanged"))return 1;
    }
    packets[2].lobbyId=8;
    if(!check(batch::group_routes(packets,routes)&&routes.count==3
        &&routes.groups[0].indices[1]==4&&routes.groups[2].indices[0]==2,"lobby generations never merged"))return 1;
    for(auto& p:packets){p.lobbyId=7;p.targetPlatformId=9;p.payloadSize=batch::kPacketCapacity;}
    packets[8].payloadSize=4;
    if(!check(batch::group_routes(packets,routes)&&routes.count==2
        &&routes.groups[0].count==7&&routes.groups[1].indices[0]==7,"budget split cannot overtake earlier same-route packet"))return 1;
    packets[0].payloadSize=3;
    if(!check(!batch::group_routes(packets,routes)&&routes.count==0,"invalid snapshot fails atomically"))return 1;
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);releaseFirst=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    dawn::steam::runtime::social::BoundedBatchSender<int,64,batch::kCapacity> sender;
    if(!check(entered && releaseFirst && finished && sender.start(&receive) && sender.enqueue(0)
        && WaitForSingleObject(entered,3000)==WAIT_OBJECT_0,"immediate first packet, no batching timer"))return 1;
    for(int i=1;i<=64;++i)if(!check(sender.enqueue(i),"nonblocking producer while transport blocked"))return 1;
    if(!check(!sender.enqueue(65),"bounded queue rejects overflow"))return 1;
    SetEvent(releaseFirst);
    if(!check(WaitForSingleObject(finished,3000)==WAIT_OBJECT_0,"drain finishes"))return 1;
    sender.stop();
    if(!check(!sender.enqueue(66) && calls==7 && largest==12,"65 packets need seven transports, shutdown refuses work"))return 1;
    for(int i=0;i<65;++i)if(!check(delivered[static_cast<std::size_t>(i)]==i,"FIFO exactly once"))return 1;
    if(!check(sender.start(&receive),"restart after shutdown"))return 1;
    sender.stop();CloseHandle(entered);CloseHandle(releaseFirst);CloseHandle(finished);
    std::puts("PASS: rendezvous batch codec, packet boundaries, byte budget, FIFO, capacity, nonblocking producer, immediate drain and lifecycle");
    return 0;
}
