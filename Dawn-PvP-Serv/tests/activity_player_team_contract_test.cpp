#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <string_view>
#include "middleware/bap/activity_message/client_authoritative_data.h"
#include "middleware/bap/activity_message/activity_join_request_parser.h"
#include "middleware/bap/activity_message/replicate_membership.h"
#include "middleware/bap/activity_message/sensor_auth_update.h"
#include "state/activity/crucible/native_score_headline.h"
#include "state/activity/membership/transactions/internal.h"
#ifdef RUN97_CANDIDATE
#include "middleware/bap/activity_message/activity_player_assignment.h"
#include "middleware/bap/activity_message/activity_player_team_parser.h"
#endif
namespace am=dawn::middleware::bap::activity_message;
namespace bits=dawn::middleware::encoding::bits;
namespace wire=am::replicate_membership;
namespace state=dawn::state::activity::membership;
using Peer=dawn::core::protocol::ActivityPeerConnection;
void check(bool ok,const char* why){if(!ok){std::printf("FAIL: team: %s\n",why);std::exit(1);}}
std::uint64_t read(bits::Reader& r,std::uint8_t n){std::uint64_t v{};check(r.read(n,v),"reader bounds");return v;}
void put(bits::Writer& w,std::uint64_t v,std::uint8_t n){check(w.write(v,n),"writer bounds");}
void zeros(bits::Writer& w,std::size_t n){while(n){const auto step=static_cast<std::uint8_t>((std::min)(n,std::size_t{64}));put(w,0,step);n-=step;}}
void b2(bits::Writer& w,std::span<const std::byte> blob){
    put(w,0,14);put(w,1,1);put(w,blob.size(),14);
    for(auto b:blob)put(w,std::to_integer<unsigned>(b),8);put(w,0,1);
}
std::array<std::byte,20> selected_blob(unsigned team){
    std::array<std::byte,20> b{};bits::Writer w(b);
    put(w,1,3);put(w,0,1);put(w,0,1);put(w,1,1);put(w,team+1,6);put(w,0,2);
    put(w,0,3);put(w,1,1);put(w,team+1,6);put(w,0,2);put(w,1,1);
    put(w,0x1111111111111111ULL,64);put(w,0x2222222222222222ULL,64);put(w,0,5);
    check(w.bit_count()==160,"complete synthetic team blob");return b;
}
Peer parsed_peer(std::span<const std::byte> blob){
    std::array<std::byte,12000> data{};bits::Writer w(data);put(w,3,2);b2(w,blob);put(w,0,3);
    std::size_t size{};check(w.finish(size),"finish type22");
    am::client_authoritative_data::ClientAuthoritativeData parsed{};
    check(am::client_authoritative_data::parse_client_authoritative_data(std::span(data).first(size),parsed),"complete type22");
    return parsed.peerConnection;
}
void inspect_name(bits::Reader& r,const wire::PlayerPresentation& p){
    const auto n=wire::name_units(p);check(read(r,1)==unsigned(n!=0),"native name presence");
    if(n)for(size_t i=0;i<=n;++i)check(read(r,16)==p.name[i],"native UTF16 name and terminator");
}
void inspect_blob(bits::Reader& r,int assignment,int requested,std::uint64_t account,std::uint64_t character,const wire::PlayerPresentation& p={}){
    check(read(r,3)==1 && read(r,1)==0,"blob CodecB header");
    inspect_name(r,p);check(read(r,1)==1,"assignment present");
    check(static_cast<int>(read(r,6))-1==assignment,"selected assignment survives B2 -> type12");
    check(read(r,2)==0,"unrelated CodecB fields unchanged");
    inspect_name(r,p);check(read(r,1)==unsigned(p.platformId!=0),"platform presence");
    if(p.platformId)for(unsigned i=0;i<36;++i)check(read(r,8)==(i<8?(p.platformId>>(i*8))&255:0),"platform wire bytes");
    check(read(r,1)==0,"unrelated CodecA field unchanged");
    const bool present=read(r,1)!=0;
    check((present?static_cast<int>(read(r,6))-1:-1)==requested,"requested side survives type12");
    check(read(r,2)==0 && read(r,1)==1,"identity marker");
    check(read(r,64)==account && read(r,64)==character,"authenticated identities, not blob inner pair");
    check(read(r,4)==0 && read(r,present?1:7)==0,"native trailing flags and exact padding");
}
std::vector<std::byte> membership(Peer first,Peer second,bool competitive,unsigned local,bool inspect,
    am::player_assignment::Mode mode=am::player_assignment::Mode::legacy,bool names=false){
    wire::MembershipSnapshot s{};
#ifdef RUN97_CANDIDATE
    s.competitiveTeams=competitive;
    s.teamMode=mode;
#endif
    s.peerConnections[0]=first;s.peerConnections[1]=second;
    const am::client_identity::ClientIdentity a{100,-1,-1,200,300,400,500},b{101,-1,-1,201,301,401,501};
    s.localMemberSlot=static_cast<std::uint8_t>(local);s.identity=local?b:a;s.reservations[0]=local?a:b;
    s.joinedMemberMask=3;s.reservationField3WireWordMask=3;
    if(names){
        constexpr std::u16string_view firstName=u"Player-CDE0-2";
        std::copy(firstName.begin(),firstName.end(),s.players[0].name.begin());
        s.players[0].platformId=0x01100001F0000001ULL;
        for(size_t i=0;i<63;++i)s.players[1].name[i]=char16_t(0x3B1+i%10);
        s.players[1].platformId=0x01100001F0000002ULL;
    }
    std::vector<std::byte> out(wire::encoded_size(s));std::size_t size{};
    check(wire::encode_replicate_membership(s,out,size)&&size==out.size(),"two-player membership exact size");
    if(inspect){
        bits::Reader r(out);check(r.skip(65),"membership header");
        for(unsigned slot=0;slot<2;++slot){
            check(r.skip(363)&&read(r,1)==1&&read(r,1)==1,"joined row");
            for(unsigned f=0;f<15;++f){
                const bool p=f==3||f==5||f==14;check(read(r,1)==unsigned(p),"B2 presence");
                if(f==3)check(read(r,64)==300+slot,"outer account");
                if(f==5)check(read(r,64)==400+slot,"outer character");
                if(f==14){
                    check(read(r,14)==20+wire::presentation_bits(s.players[slot])/8,"dynamic player blob length");
                    const int side=mode==am::player_assignment::Mode::rumble?static_cast<int>(slot):1-static_cast<int>(slot);
                    inspect_blob(r,competitive?side:0,competitive?side:-1,300+slot,400+slot,s.players[slot]);
                }
            }
            check(read(r,1)==0 && read(r,3)==0 && read(r,1)==1 && read(r,5)==1,"detail tail");
        }
        check(r.skip(30*3)&&read(r,1)==1,"empty slots and region marker");
        check(out.size()*8-r.remaining_bits()==wire::region_block_start_bit(s),"region offset unchanged");
    }
    return out;
}
void complete_participation_contract() {
    namespace sa=am::sensor_auth_update;
    sa::Snapshot s{};s.lifetime=3;s.completeParticipationRoster=true;
    s.completeScoreboardRoster=true;s.roster.groupCount=1;s.roster.topLevelGroupCount=1;
    s.roster.playerKeyGroup=0xc2dacaf7;
    std::array<std::uint8_t,12> types{},flags{};types.fill(13);flags.fill(3);
    std::array<std::uint16_t,12> slots{};for(std::size_t i=0;i<12;++i)slots[i]=static_cast<std::uint16_t>(7+i);
    s.roster.groups[0]={0xc2dacaf7,types,flags,slots};
    std::array<std::byte,8192> out{};std::size_t n{};
    for(std::size_t count: {std::size_t{0},std::size_t{1},std::size_t{6},std::size_t{12}}) {
        s.participationPlayers={};s.scoreboardPlayers={};s.scoreboardPlayerCount=static_cast<std::uint8_t>(count);
        for(std::size_t i=0;i<count;++i) {
            const auto side=static_cast<std::uint8_t>(i);
            s.participationPlayers[i]={100+i,static_cast<std::int32_t>(i),side,static_cast<std::uint8_t>(17+i),i%2!=0};
            s.scoreboardPlayers[i]={100+i,side,0,0,0};
        }
        check(sa::encode_sensor_auth_update(s,out,n)&&n>0,"complete sparse/native participation roster encodes");
        if(count) {
            auto bad=s;bad.participationPlayers[0].faction=0;
            check(!sa::encode_sensor_auth_update(bad,out,n)&&n==0,"wrong participation faction rejects atomically");
            bad=s;bad.participationPlayers[0].key=0;
            check(!sa::encode_sensor_auth_update(bad,out,n),"partial peer snapshot rejects atomically");
        }
        if(count>1) {auto bad=s;bad.participationPlayers[1].key=100;
            check(!sa::encode_sensor_auth_update(bad,out,n),"duplicate native participation identity rejects");}
    }
    for (bool present : {false,true}) {
        sa::Snapshot row{};row.playerKey=present?0x1122334455667788ULL:0;
        row.playerAssignment=present?11:0;row.teamFaction=present?28:0;
        row.hasRegion=present;row.region=4;row.awaitClientSync=!present;
        std::array<std::byte,128> body{};bits::Writer writer(body);
        check(sa::write_auth_body(writer,row,0,13,7,true),"export native13 production body");
        const auto bitCount=writer.bit_count();std::size_t bytes{};check(writer.finish(bytes),"finish native13 fixture");
        std::ofstream file(present?"build/native-pvp-20261002/ui/participation-occupied.bin":"build/native-pvp-20261002/ui/participation-empty.bin",std::ios::binary);
        file.write(reinterpret_cast<const char*>(body.data()),static_cast<std::streamsize>(bytes));
        std::ofstream meta(present?"build/native-pvp-20261002/ui/participation-occupied.bits":"build/native-pvp-20261002/ui/participation-empty.bits");meta<<bitCount;
    }
    slots[11]=17;check(!sa::encode_sensor_auth_update(s,out,n),"duplicate authored player slot rejects");
    slots[11]=18;flags[11]=1;check(!sa::encode_sensor_auth_update(s,out,n),"missing auth player slot rejects");
}
int main(int argc,char** argv){
    complete_participation_contract();
    {
        namespace cr=dawn::state::activity::crucible;
        cr::host::View v{};v.mode=cr::match_rules::Mode::rumble;v.phase=cr::host::Phase::playing;v.count=3;
        v.rows[0].player=11;v.rows[1].player=55;v.rows[2].player=99;
        v.rows[0].score=3;v.rows[1].score=9;v.rows[2].score=7;
        check(cr::native_score_headline(v,0)==8 && cr::native_score_headline(v,1)==6
              && cr::native_score_headline(v,2)==7,"placement is score rank, not row order or faction");
        v.rows[2].score=9;
        check(cr::native_score_headline(v,0)==8 && cr::native_score_headline(v,1)==14
              && cr::native_score_headline(v,2)==14,"native tied-first and competition third");
        v.rows[0].score=9;
        check(cr::native_score_headline(v,0)==14,"three equal scores use tied first");
        v.count=1;check(cr::native_score_headline(v,0)==6,"solo first place");
        v.count=12;
        for(std::size_t i=0;i<12;++i){v.rows[i].player=100+i;v.rows[i].score=static_cast<std::uint32_t>(12-i);}
        check(cr::native_score_headline(v,7)==13 && cr::native_score_headline(v,8)==-1
              && cr::native_score_headline(v,11)==-1,"native authored eighth bound; later places have no fabricated label");
        v.rows[8].score=v.rows[7].score;
        check(cr::native_score_headline(v,7)==21,"tied eighth uses last authored label");
        v.phase=cr::host::Phase::results;check(cr::native_score_headline(v,0)==-1,"no playing placement during results");
        v.phase=cr::host::Phase::playing;v.mode=cr::match_rules::Mode::clash;
        check(cr::native_score_headline(v,0)==-1,"Clash has no Rumble headline");
        v.mode=cr::match_rules::Mode::rumble;v.rows[11].player=v.rows[0].player;
        check(cr::native_score_headline(v,0)==-1,"duplicate identity refuses placement");
    }
    const auto one=selected_blob(1),zero=selected_blob(0);
    Peer a=parsed_peer(one),b=parsed_peer(zero);
    const auto out=membership(a,b,true,0,true);
    check(membership(a,b,true,1,true).size()==out.size(),"reverse local-slot direction");
#ifdef RUN97_CANDIDATE
    namespace assign=am::player_assignment;
    for(unsigned local=0;local<2;++local){
        const auto named=membership(a,b,true,local,true,assign::Mode::rumble,true);
        check(named.size()>out.size(),"names increase body without shifting canonical identities");
    }
    wire::MembershipSnapshot invalidName{};invalidName.players[0].name.fill(u'X');
    std::array<std::byte,16000> untouched{};untouched.fill(std::byte{0xA5});
    size_t nameSize=99;check(!wire::encode_replicate_membership(invalidName,untouched,nameSize),"unterminated name rejected");
    for(auto x:untouched)check(x==std::byte{0xA5},"invalid name leaves destination untouched");
    check(membership(b,b,true,0,true,assign::Mode::rumble).size()==out.size(),
          "Rumble identical orbit sides become independent wire assignments");
    check(membership(b,b,true,1,true,assign::Mode::rumble).size()==out.size(),
          "Rumble assignment agrees when the remote player is local");
    for(std::size_t slot=0;slot<12;++slot) {
        const auto side=assign::resolve(true,assign::Mode::rumble,{},slot);
        check(side.publishRequestedTeam&&side.faction==slot&&side.teamIndex==slot,
              "Rumble sparse canonical slots do not require a team echo");
    }
    check(!assign::resolve(true,assign::Mode::rumble,{},32).publishRequestedTeam,
          "missing canonical membership does not fabricate a Rumble side");
    auto pendingClash=b.playerTeam;pendingClash.requestedTeam=1;
    const auto clash=assign::resolve(true,assign::Mode::clash,pendingClash,0);
    check(clash.publishRequestedTeam&&clash.faction==1,"Clash requested Bravo survives stale Alpha echo");
    pendingClash.assignment=11;
    check(assign::resolve(true,assign::Mode::clash,pendingClash,0)==clash,
          "switching from Rumble cannot leak a stale free-for-all side into Clash");
    pendingClash.hasRequestedTeam=false;pendingClash.assignment=1;
    check(assign::resolve(true,assign::Mode::clash,pendingClash,0)==clash,
          "Clash sparse accepted side remains usable");
    check(assign::resolve(false,assign::Mode::rumble,a.playerTeam,1)==assign::Assignment{},
          "noncompetitive destinations ignore stale Rumble mode");
    check(a.playerTeam.hasAssignment&&a.playerTeam.assignment==1&&a.playerTeam.requestedTeam==1,"typed incoming selected side");
    check(b.playerTeam.assignment==0&&b.playerTeam.requestedTeam==0,"independent second player");
    check(membership(a,b,false,0,true)==membership({}, {},false,0,true),"cooperative bytes unchanged despite saved PvP selection");
    check(!assign::competitive_destination("edz")&&!assign::competitive_destination("social_tower")
        &&!assign::competitive_destination("social_farm")&&assign::competitive_destination("pvp_ness"),"PvP-only policy");
    auto invalid=a.playerTeam;invalid.assignment=31;check(!assign::resolve(true,invalid).publishRequestedTeam,"out of authority range");
    auto rumble=a.playerTeam;rumble.assignment=rumble.requestedTeam=11;
    const auto rumbleAssignment=assign::resolve(true,rumble);
    check(rumbleAssignment.publishRequestedTeam && rumbleAssignment.faction==11
        && rumbleAssignment.teamIndex==11,"native twelve-side Rumble choice preserved");
    rumble.assignment=rumble.requestedTeam=12;
    check(!assign::resolve(true,rumble).publishRequestedTeam,"beyond native Rumble sides refused");
    invalid=a.playerTeam;invalid.requestedTeam=0;check(!assign::resolve(true,invalid).publishRequestedTeam,"transitional mismatch not guessed");
    invalid=a.playerTeam;invalid.assignment=invalid.requestedTeam=-1;check(!assign::resolve(true,invalid).publishRequestedTeam,"explicit unset uses existing default");
    state::MembershipState stored{};stored.peerConnection=a;
    state::AuthoritativeUpdate delta{};delta.peerConnection.hasFlags=true;delta.peerConnection.flags=16;
    auto merged=state::transactions::merge(stored,delta);
    check(merged.peerConnection.playerTeam==a.playerTeam,"ready-only update retains choice");
    delta.peerConnection=b;merged=state::transactions::merge(stored,delta);
    check(merged.peerConnection.playerTeam==b.playerTeam&&!state::transactions::equal_authoritative(stored,merged),"team update changes publication revision");
    delta={};delta.peerConnection.playerTeam.hasAssignment=true;delta.peerConnection.playerTeam.assignment=-1;
    merged=state::transactions::merge(stored,delta);
    check(merged.peerConnection.playerTeam.assignment==-1&&merged.peerConnection.playerTeam.requestedTeam==1,"explicit unset and sparse retention");
    for(unsigned offset=0;offset<8;++offset){
        std::array<std::byte,400> packet{};bits::Writer w(packet);zeros(w,offset);b2(w,one);
        bits::Reader r(packet,w.bit_count());check(r.skip(offset),"unaligned B2 offset");Peer p{};
        check(am::client_authoritative_data::read_peer_connection_branch(r,p)&&p==a,"unaligned B2 native bit grammar");
        for(std::size_t end=offset;end<w.bit_count();++end){bits::Reader t(packet,end);check(t.skip(offset),"truncated prefix");p=a;check(!am::client_authoritative_data::read_peer_connection_branch(t,p)&&p==Peer{},"no partial metadata on any bit truncation");}
    }
    std::array<std::byte,500> join{};bits::Writer jw(join);put(jw,7,32);put(jw,99,64);put(jw,123,64);zeros(jw,298);b2(jw,one);
    std::size_t js{};check(jw.finish(js),"join finish");am::JoinRequest jr{};
    check(am::join_request::parse_join_request(std::span(join).first(js),jr)&&jr.peerConnection.playerTeam==a.playerTeam,"join and type22 retain identical choice");
    check(am::join_request::parse_join_request(std::span(join).first(20),jr)&&jr.peerConnection==Peer{},"legacy join prefix preserved");
    check(parsed_peer(std::span<const std::byte>{})==Peer{},"empty optional blob accepted without team");
    auto unknown=one;unknown[0]=std::byte{0xe0};check(parsed_peer(unknown)==Peer{},"unknown incarnation accepted without speculative metadata");
    for(unsigned side=0;side<2;++side){
        am::sensor_auth_update::Snapshot s{};const auto selected=assign::resolve(true,side?a.playerTeam:b.playerTeam);
        s.playerAssignment=selected.faction;s.playerTeamIndex=selected.teamIndex;s.teamFaction=selected.faction;
        std::array<std::byte,100> bytes{};bits::Writer w(bytes);
        check(am::sensor_auth_update::write_auth_body(w,s,0,13,0,true)&&w.bit_count()==192,"participation body width unchanged");
        bits::Reader r(bytes);check(r.skip(46)&&read(r,5)==side+1,"type13 assignment matches type12");
        bits::Reader t(bytes);check(t.skip(125)&&read(t,6)==selected.faction+3,"participation combat faction");
    }
    for(unsigned side=0;side<13;++side){
        am::sensor_auth_update::Snapshot s{};
        s.playerTeamIndex=side%12;
        s.teamFaction=static_cast<std::uint8_t>(side<12?am::sensor_auth_update::kFirstCrucibleFaction+side:0);
        std::array<std::byte,100> bytes{};bits::Writer w(bytes);
        check(am::sensor_auth_update::write_auth_body(w,s,0,13,0,true)&&w.bit_count()==192,"faction body width unchanged");
        bits::Reader r(bytes);check(r.skip(125)&&read(r,6)==s.teamFaction+3,"all twelve PvP factions and PvE zero round trip");
    }
    am::sensor_auth_update::Snapshot scoreboard{};
    for (const bool crucible : {false, true}) {
        for (const bool awaiting : {false, true}) {
            for (const bool region : {false, true}) {
                am::sensor_auth_update::Snapshot respawn{};
                respawn.honorNativeRespawnDelay = crucible;
                respawn.awaitClientSync = awaiting;
                respawn.hasRegion = region;
                respawn.region = 7;
                std::array<std::byte, 40> bytes{};
                bits::Writer writer(bytes);
                check(am::sensor_auth_update::write_auth_body(writer,respawn,0,13,0,true)
                      && writer.bit_count()==(region ? 224U : 192U),
                      "respawn policy preserves participation wire width");
                bits::Reader reader(bytes,writer.bit_count());
                check(reader.skip(143+(region ? 32 : 0)),"respawn flag offset");
                check(read(reader,1)==(crucible ? 0U : 1U),
                      "Crucible honors native death delay; PvE retains fallback");
                check(read(reader,4)==(awaiting ? 2U : 0U),
                      "initial client loading gate independent of respawn delay");
                check(read(reader,4)==0 && read(reader,8)==128
                      && read(reader,32)==0x80000000U,
                      "respawn change leaves optional delays and trailing fields intact");
            }
        }
    }
    scoreboard.competitiveScoreboard=true;
    scoreboard.playerTeamIndex=0;
    scoreboard.teamFaction=am::sensor_auth_update::kFirstCrucibleFaction;
    check(am::sensor_auth_update::auth_body_bits(scoreboard,0,16,0,false)==124,
          "matched two-team scoreboard keeps its native body");
    scoreboard.playerTeamIndex=11;
    scoreboard.teamFaction=am::sensor_auth_update::kFirstCrucibleFaction+11;
    check(am::sensor_auth_update::auth_body_bits(scoreboard,0,16,0,false)==7,
          "Rumble side keeps absent package scoreboard");
    std::array<std::byte,2> scoreboardBytes{};bits::Writer scoreboardWriter(scoreboardBytes);
    check(am::sensor_auth_update::write_auth_body(scoreboardWriter,scoreboard,0,16,0,false)
          && scoreboardWriter.bit_count()==7,"Rumble fallback body matches declared width");
    scoreboard.playerKey=0x1234;
    scoreboard.competitiveScoreboard=true;
    scoreboard.completeScoreboardRoster=true;
    scoreboard.scoreboardPlayerCount=3;
    scoreboard.scoreboardTeamCount=12;
    scoreboard.scoreboardPlayers[0]={0x1234,11};
    scoreboard.scoreboardPlayers[1]={0x5678,0};
    scoreboard.scoreboardPlayers[2]={0x9ABC,5};
    constexpr std::size_t dynamicBits=1+5+3*((1+64)+3+(1+6)+1)
                                      +1+4+12*((1+6)*2+1+1)+5;
    check(am::sensor_auth_update::auth_body_bits(scoreboard,0,16,0,false)==dynamicBits,
          "complete native twelve-side scoreboard width");
    std::array<std::byte,64> fullScoreboardBytes{};
    bits::Writer fullWriter(fullScoreboardBytes);
    check(am::sensor_auth_update::write_auth_body(fullWriter,scoreboard,0,16,0,false)
          && fullWriter.bit_count()==dynamicBits,"complete native scoreboard body");
    bits::Reader fullReader(fullScoreboardBytes,fullWriter.bit_count());
    check(read(fullReader,1)==1&&read(fullReader,5)==3,"scoreboard player table count");
    for(unsigned row=0;row<3;++row){
        check(read(fullReader,1)==1&&read(fullReader,64)==scoreboard.scoreboardPlayers[row].key,
              "authenticated player row key");
        check(read(fullReader,3)==0&&read(fullReader,1)==1
              &&read(fullReader,6)==0
              &&read(fullReader,1)==1,"active player row with no headline and absent stats");
    }
    check(read(fullReader,1)==1&&read(fullReader,4)==12,"scoreboard team table count");
    for(unsigned row=0;row<12;++row){
        check(read(fullReader,1)==1&&read(fullReader,6)==20+row
              &&read(fullReader,1)==1&&read(fullReader,6)==0
              &&read(fullReader,1)==(row==0||row==5||row==11?1U:0U)&&read(fullReader,1)==0,
              "only occupied native factions are active, stat list absent");
    }
    check(read(fullReader,5)==0&&fullReader.remaining_bits()==0,
          "scoreboard trailing optionals stay absent");
    scoreboard.scoreboardStats=true;
    scoreboard.scoreboardPlayers[0].score=7;scoreboard.scoreboardPlayers[0].kills=7;
    scoreboard.scoreboardPlayers[0].deaths=3;
    scoreboard.scoreboardTeams[11]={7,7,3};
    std::array<std::byte,512> scoreBytes{};bits::Writer scoreWriter(scoreBytes);
    check(am::sensor_auth_update::write_auth_body(scoreWriter,scoreboard,0,16,0,false),"scored roster encodes");
    bits::Reader sr(scoreBytes,scoreWriter.bit_count());
    check(read(sr,1)==1&&read(sr,5)==3,"scored player count");
    for(unsigned row=0;row<3;++row) {
        check(read(sr,1)==1&&read(sr,64)==scoreboard.scoreboardPlayers[row].key
            &&read(sr,2)==0&&read(sr,1)==1&&read(sr,7)==8,"native player stat array");
        for(unsigned col=0;col<8;++col) {
            const bool present=col==0||col==5||col==7;
            check(read(sr,1)==(present?1U:0U),"player stat presence");
            if(present) check(std::bit_cast<float>(static_cast<std::uint32_t>(read(sr,32)))
                ==float(row==0?(col==7?3:7):0),"native player score/kills/deaths value");
        }
        check(read(sr,1)==1&&read(sr,6)==0
            &&read(sr,1)==1,"scored player is active");
    }
    check(read(sr,1)==1&&read(sr,4)==12,"scored team count");
    for(unsigned row=0;row<12;++row) {
        check(read(sr,1)==1&&read(sr,6)==20+row&&read(sr,1)==1&&read(sr,6)==0
            &&read(sr,1)==(row==0||row==5||row==11?1U:0U)&&read(sr,1)==1&&read(sr,5)==4,
            "occupied native teams are active, unused Rumble slots hidden");
        for(unsigned col=0;col<4;++col) {
            check(read(sr,1)==(col!=1?1U:0U),"team stat presence");
            if(col!=1)check(std::bit_cast<float>(static_cast<std::uint32_t>(read(sr,32)))
                ==float(row==11?(col==3?3:7):0),"native team score/kills/deaths value");
        }
    }
    check(read(sr,5)==0&&sr.remaining_bits()==0,"scored roster aligns through final field");
    scoreboard.scoreboardLayout=0x5DEA3FF8U;
    std::array<std::byte,512> layoutBytes{};bits::Writer layoutWriter(layoutBytes);
    check(am::sensor_auth_update::write_auth_body(layoutWriter,scoreboard,0,16,0,false)
          && layoutWriter.bit_count()==scoreWriter.bit_count()+32,
          "native scoreboard layout adds exactly one32bit value");
    bits::Reader lr(layoutBytes,layoutWriter.bit_count());
    for(std::size_t left=scoreWriter.bit_count()-5;left;) {
        const auto width=static_cast<std::uint8_t>(std::min<std::size_t>(left,64));
        (void)read(lr,width);left-=width;
    }
    check(read(lr,2)==0 && read(lr,1)==1 && read(lr,32)==0x5DEA3FF8U
          && read(lr,2)==0 && lr.remaining_bits()==0,
          "808099F9 ord4 native column selector preserves neighboring optional fields");
    scoreboard.scoreboardPlayers[2].key=0x5678;
    check(am::sensor_auth_update::auth_body_bits(scoreboard,0,16,0,false)==7,
          "duplicate player key refuses complete scoreboard");
    am::sensor_auth_update::Snapshot placements{};
    placements.cruciblePlacements={0x39774EE1U,1,{13,14,15},false};
    check(am::sensor_auth_update::auth_body_bits(placements,0x39774EE1U,4,13,false)==0,
          "Control placement experiment is off by default");
    placements.cruciblePlacements.enabled=true;
    for (std::uint16_t slot=13;slot<=15;++slot) {
        std::array<std::byte,40> placementBytes{};bits::Writer placementWriter(placementBytes);
        check(am::sensor_auth_update::auth_body_bits(placements,0x39774EE1U,4,slot,false)==252
              && am::sensor_auth_update::write_auth_body(placementWriter,placements,0x39774EE1U,4,slot,false)
              && placementWriter.bit_count()==252,"Control placement body stays aligned");
    }
    check(am::sensor_auth_update::auth_body_bits(placements,0x39774EE1U,4,12,false)==0
          && am::sensor_auth_update::auth_body_bits(placements,0x39774EE0U,4,13,false)==0,
          "Control placement experiment cannot claim other objects or groups");
    {
        am::sensor_auth_update::Snapshot clock{};
        std::array<std::byte, 32> bytes{};
        bits::Writer nativeClockWriter(bytes);
        check(am::sensor_auth_update::write_auth_body(nativeClockWriter,clock,0,8,0,false)
              && nativeClockWriter.bit_count()==35,"orbit retains native empty configuration body");
        check(std::all_of(bytes.begin(),bytes.end(),[](std::byte b){return b==std::byte{};}),
              "orbit configuration has no scripted clock override");
        clock.nativeClockVisible=true;
        bits::Writer visibleWriter(bytes);
        check(am::sensor_auth_update::auth_body_bits(clock,0,8,0,false)==107
              && am::sensor_auth_update::write_auth_body(visibleWriter,clock,0,8,0,false)
              && visibleWriter.bit_count()==107,"native configuration variable body exact107bits");
        std::size_t visibleSize{};check(visibleWriter.finish(visibleSize)&&visibleSize==14,"configuration variable pads only trailing fivebits");
        constexpr std::array<unsigned char,14> golden{0x41,0xF7,0x83,0xE1,0xC5,0xCF,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
        for(std::size_t i=0;i<golden.size();++i)
            check(std::to_integer<unsigned char>(bytes[i])==golden[i],"native80804F10/80804F16 schema golden clock variable");
    }
    if(argc>1){
        std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
        check(bytes.size()==10888,"saved native B2 root");
        unsigned count{};for(unsigned i=0;i<4;++i)count|=static_cast<unsigned char>(bytes[512+i])<<(8*i);
        check(count==121,"observed native blob length");
        auto native=std::span(reinterpret_cast<const std::byte*>(bytes.data()+516),count);
        check(parsed_peer(native).playerTeam==a.playerTeam,"captured native names/platform/stats decode to selected side");
    }
    if(argc>2){std::ofstream f(argv[2],std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));}
#else
    (void)argc;(void)argv;
#endif
    std::puts("PASS: native team input, join/type22, transaction retention, per-player type12/type13, reverse slots, cooperative compatibility, malformed bounds");
}
