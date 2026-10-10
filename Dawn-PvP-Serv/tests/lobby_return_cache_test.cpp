#include "steam/runtime/social/lobby_cache.h"
#include <cstdio>
namespace c=dawn::steam::runtime::social::lobby_cache;
namespace w=dawn::middleware::social::realtime;
static int checks{};
static bool check(bool value,const char* label){
 ++checks;if(!value)std::printf("FAIL: %s\n",label);return value;
}
int main(){
 static c::Cache cache;static w::Response response;
 w::Lobby regular{},solo{},temporary{},tower{};
 regular.id=1;regular.type=1;regular.ownerPlatformId=101;regular.memberCount=1;regular.members[0].platformId=101;
 solo=regular;solo.id=12;solo.type=3;temporary=solo;temporary.id=13;
 tower=solo;tower.id=11;tower.ownerPlatformId=102;tower.memberCount=2;tower.members[1].platformId=102;
 cache.remember(regular);cache.remember(temporary);cache.remember(solo);
 response.result=1;response.lobby=tower;response.eventCount=1;
 auto& e=response.events[0];e.kind=w::EventKind::memberChanged;e.lobbyId=temporary.id;e.memberPlatformId=101;
 const std::uint32_t left=2;e.payloadSize=sizeof(left);std::memcpy(e.payload.data(),&left,sizeof(left));
 c::store_response(cache,response,&solo,101);
 if(cache.snapshot(tower.id).ownerPlatformId!=102 || cache.snapshot(tower.id).memberCount!=2
    ||cache.snapshot(temporary.id).id!=0 || !cache.owned(solo.id,101)||cache.snapshot(regular.id).id!=regular.id){
    std::puts("FAIL: accepted Tower return absent from full lobby cache after exact temporary departure");return 1;
 }
 std::puts("PASS: accepted Tower return cached before native callback, temporary retired, other memberships retained");
 const auto seed=[&](){cache={};cache.remember(regular);cache.remember(temporary);cache.remember(solo);};
 const auto present=[&](std::uint64_t id){return cache.snapshot(id).id==id;};
 bool ok=true;
 seed();e.memberPlatformId=102;c::store_response(cache,response,&solo,101);
 ok&=check(present(temporary.id)&&!present(tower.id),"remote LEFT must not evict a live membership");
 seed();e.memberPlatformId=101;e.lobbyId=solo.id;c::store_response(cache,response,&solo,101);
 ok&=check(present(solo.id)&&present(temporary.id),"selected membership survives stale LEFT");
 seed();response.lobby=temporary;e.lobbyId=temporary.id;c::store_response(cache,response,&solo,101);
 ok&=check(present(temporary.id),"operation target survives stale LEFT");
 seed();response.lobby=tower;e.payloadSize=0;c::store_response(cache,response,&solo,101);
 ok&=check(present(temporary.id)&&!present(tower.id),"missing change payload must not evict");
 seed();e.payloadSize=sizeof(left);const std::uint32_t joined=1;
 std::memcpy(e.payload.data(),&joined,sizeof(joined));c::store_response(cache,response,&solo,101);
 ok&=check(present(temporary.id)&&!present(tower.id),"JOIN must not evict");
 seed();const std::uint32_t kicked=4;std::memcpy(e.payload.data(),&kicked,sizeof(kicked));
 c::store_response(cache,response,&solo,101);
 ok&=check(!present(temporary.id)&&present(tower.id)&&present(solo.id)&&present(regular.id),"explicit self KICK retires only its lobby");
 seed();e.kind=w::EventKind::lobbyDestroyed;e.payloadSize=0;c::store_response(cache,response,&solo,101);
 ok&=check(!present(temporary.id)&&present(tower.id),"destroyed temporary lobby releases exact slot");
 seed();response.eventCount=response.events.size()+1;c::retire_response_departures(cache,response,&solo,101);
 ok&=check(present(temporary.id),"invalid event count must not retire entries");
 w::Request query{};static w::Response reply{};
 query.operation=w::Operation::queryLobby;query.lobbyId=temporary.id;query.cursor=77;
 reply.result=15;reply.cursor=query.cursor;
 ok&=check(c::authoritative_absence(query,reply),"exact authenticated absence accepted");
 reply.result=2;ok&=check(!c::authoritative_absence(query,reply),"generic failure never authorizes eviction");
 reply.result=1;ok&=check(!c::authoritative_absence(query,reply),"successful query never means absence");
 reply.result=15;reply.cursor=78;ok&=check(!c::authoritative_absence(query,reply),"advanced event cursor rejected");
 reply.cursor=77;reply.eventCount=1;ok&=check(!c::authoritative_absence(query,reply),"event-draining reply rejected");
 reply.eventCount=0;reply.lobby=temporary;ok&=check(!c::authoritative_absence(query,reply),"nonempty membership rejected");
 reply.lobby={};query.lobbyId=0;ok&=check(!c::authoritative_absence(query,reply),"selected query is not exact absence");
 query.lobbyId=temporary.id;query.operation=w::Operation::joinLobby;
 ok&=check(!c::authoritative_absence(query,reply),"mutation response never authorizes fallback eviction");
 seed();w::Lobby malformed=tower;malformed.memberCount=malformed.members.size()+1;
 ok&=check(!cache.can_remember(malformed)&&!cache.can_remember(tower)&&cache.can_remember(solo),"cache capacity and shape guards preserved");
 if(!ok)return 1;
 std::printf("PASS: %d return-cache safety checks; stale/remote departures, malformed events, bounded membership reconciliation\n",checks);
 return 0;
}
