#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "server/bap/encrypted/push/activity/activity_roster_push.h"
#include "server/bap/encrypted/push/activity/activity_notification_frame.h"
#include "server/bap/encrypted/push/activity/internal.h"
#include "middleware/bap/activity_message/native/activity_clock_authority.h"
#include "state/activity/bubble_authority/runtime.h"
#include "state/activity/mission_host/mission_host.h"
namespace pub = dawn::server::bap::encrypted::push::activity;
namespace msg = dawn::middleware::bap::activity_message::sensor_auth_update;
namespace cfg = dawn::middleware::bap::activity_message::native::activity_clock;
namespace {
bool ownsClock=true, runningClock=true, failBody=false;
unsigned failFrame=0, calls=0;
std::array<std::uint32_t,2> types{};
std::array<std::byte,5> configuration{};
void check(bool ok,const char* why) { if(!ok) { std::printf("FAIL: %s\n",why);std::exit(1); } }
}
namespace dawn::server::bap::encrypted::push::activity {
RosterOutcome build_roster_snapshot(Session& session,Scratch&,message::Snapshot& snapshot,
    std::span<char>,std::size_t& length,bool,const core::protocol::ActivityPlayerTeam*) noexcept {
    snapshot.nativeCountdown=ownsClock; snapshot.nativeCountdownRunning=runningClock; length=0;
    session.activityRosterGroups=9;session.activityRosterSends=4;session.activityRosterState=8;
    return RosterOutcome::published;
}
void report_roster_push(Session&,const message::Snapshot&,std::string_view,std::size_t,
    std::int32_t,RosterOutcome) noexcept {}
bool append_notification_frame(Scratch&,std::uint64_t,std::uint32_t type,
    std::span<const std::byte> body,std::span<const std::byte,state::kAesKeySize>,
    std::span<const std::byte,state::kBapNonceSize> nonce,std::span<std::byte> response,
    std::size_t& written) noexcept {
    check(calls<types.size(),"no extra frames");types[calls]=type;++calls;
    check(std::to_integer<unsigned>(nonce[0])==calls-1,"consecutive nonce for each frame");
    if(type==cfg::kMessageType) {
        check(body.size()==configuration.size(),"clock configuration frame size");
        std::memcpy(configuration.data(),body.data(),body.size());
    }
    if(calls==failFrame) return false;
    response[written++]=std::byte{static_cast<unsigned char>(type)};
    return true;
}
}
namespace dawn::middleware::bap::activity_message::sensor_auth_update {
bool encode_sensor_auth_update(const Snapshot&,std::span<std::byte> out,std::size_t& written) noexcept {
    written=0;if(failBody)return false;out[0]=std::byte{0x55};written=1;return true;
}
}
namespace dawn::middleware::secure_channel {
void advance_nonce(std::span<std::byte,state::kBapNonceSize> nonce) noexcept {
    nonce[0]=std::byte{static_cast<unsigned char>(std::to_integer<unsigned>(nonce[0])+1)};
}
}
namespace dawn::state::activity::bubble_authority {
bool select_grant(std::uint64_t,std::int32_t,Grant&) noexcept {return false;}
void record_grant(std::uint64_t,const Grant&) noexcept {}
}
namespace dawn::state::activity::mission_host {
void note_authority(std::uint64_t,std::uint64_t) noexcept {}
}
int main() {
    static dawn::server::bap::Scratch scratch;
    static dawn::server::bap::Session session;
    std::array<std::byte,dawn::state::kAesKeySize> key{};
    for(unsigned test=0;test<6;++test) {
        session={};session.activityPatchEpochSeen=true;session.activityRosterGroups=1;
        session.activityRosterSends=2;session.activityRosterState=3;
        calls=0;types={};ownsClock=test!=4;runningClock=test!=5;failFrame=test==1?1:test==2?2:0;failBody=test==3;
        std::array<std::byte,dawn::state::kBapNonceSize> nonce{};
        std::array<std::byte,32> output{};output.fill(std::byte{0xA5});std::size_t written=3;
        const bool result=pub::append_roster_notification(session,scratch,key,nonce,output,written,false);
        const bool expected=test==0||test==4||test==5;
        check(result==expected,"publication result");
        for(unsigned i=0;i<3;++i)check(output[i]==std::byte{0xA5},"existing response preserved");
        if(expected) {
            const unsigned frames=ownsClock?2:1;
            check(written==3+frames&&std::to_integer<unsigned>(nonce[0])==frames,"all frames and nonces staged");
            check(types[0]==(ownsClock?2U:5U)&&(!ownsClock||types[1]==5),"native configuration precedes authority");
            if(ownsClock) {
                std::array<std::byte,5> exact{};std::size_t size{};
                check(cfg::encode(runningClock?cfg::kRunningCrucible:cfg::Configuration{false,0.0F},exact,size)&&exact==configuration,"production clock body unchanged in transport");
            }
            check(session.activityRosterStaged.staged,"roster commit remains deferred");
            pub::discard_staged_roster(session);
        } else {
            check(written==3&&nonce[0]==std::byte{0},"failed pair restores response length and nonce");
            check(!session.activityRosterStaged.staged,"failed pair owes no commit");
            if(test==2||test==3)check(output[3]==std::byte{0},"partial clock frame wiped");
        }
        check(session.activityRosterGroups==1&&session.activityRosterSends==2&&session.activityRosterState==3,
            "failed or discarded pair restores roster counters");
    }
    std::puts("PASS: actual native clock/roster publication ordering, nonce rollback, partial-frame wipe, discard and non-clock behavior");
}
