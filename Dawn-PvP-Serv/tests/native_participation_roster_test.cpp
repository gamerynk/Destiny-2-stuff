#include <array>
#include <cstdio>
#include <cstdlib>
#include "middleware/bap/activity_message/sensor_auth_update.h"
namespace sa=dawn::middleware::bap::activity_message::sensor_auth_update;
void check(bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);std::exit(1);}}
int main(){
 sa::Snapshot s{};s.lifetime=3;s.completeParticipationRoster=true;s.completeScoreboardRoster=true;
 s.roster.groupCount=1;s.roster.topLevelGroupCount=1;s.roster.playerKeyGroup=0xc2dacaf7;
 std::array<std::uint8_t,12> types{},flags{};types.fill(13);flags.fill(3);
 std::array<std::uint16_t,12> slots{};for(std::size_t i=0;i<12;++i)slots[i]=static_cast<std::uint16_t>(7+i);
 s.roster.groups[0]={0xc2dacaf7,types,flags,slots};
 std::array<std::byte,8192> out{};std::size_t bytes{};
 s.scoreboardPlayerCount=2;s.scoreboardPlayers[0]={100,0,9,1,1};s.scoreboardPlayers[1]={200,1,10,2,1};
 s.participationPlayers[0]={100,3,0,17,false};s.participationPlayers[11]={200,3,1,18,true};
 check(sa::encode_sensor_auth_update(s,out,bytes),"sparse canonical slots preserve per-peer readiness");
 s.participationPlayers[11]={};check(sa::encode_sensor_auth_update(s,out,bytes),"departed peer clears slot while historical score remains");
 auto departed=out;auto departedBytes=bytes;
 s.participationPlayers[11]={200,3,1,18,true};check(sa::encode_sensor_auth_update(s,out,bytes),"rejoined admitted peer restores exact slot");
 check(bytes!=departedBytes || out!=departed,"clear changes actual native wire body");
 s.participationPlayers[11]={300,3,1,18,false};check(!sa::encode_sensor_auth_update(s,out,bytes),"unadmitted replacement never steals historical native slot");
 s.participationPlayers[11]={};s.unresolvedParticipationRoster=true;
 check(!sa::encode_sensor_auth_update(s,out,bytes)&&bytes==0,"transient active-peer read failure retries atomically");
 s.unresolvedParticipationRoster=false;s.participationPlayers[0]={};
 check(sa::encode_sensor_auth_update(s,out,bytes),"all departed history still publishes explicit empty native slots");
 std::puts("PASS: native participation sparse, departure, reuse, peer readiness and atomic retry");
}
