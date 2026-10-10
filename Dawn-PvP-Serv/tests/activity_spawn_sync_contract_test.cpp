#include <cstdint>
#include <cstdio>
#include "server/bap/encrypted/push/activity/activity_spawn_sync.h"
#include "server/bap/encrypted/push/activity/activity_spawn_join_ack_policy.h"
#include "server/bap/encrypted/push/activity/activity_roster_generation_policy.h"

namespace {
namespace sync = dawn::server::bap::encrypted::push::activity::spawn_sync;
namespace generation = dawn::server::bap::encrypted::push::activity::roster_generation;
bool require(bool condition, const char* name) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}
}
int main() {
    using Outcome = sync::ArmOutcome;
    constexpr std::uint64_t player=0x0110000100000002ULL, other=0x01100001DEADBEEFULL;
    constexpr std::uint64_t activity=0x9EAA000100200046ULL;
    static dawn::server::bap::Session session{};
    static dawn::state::activity::SessionRecord record{};
    session.authenticated=true;
    session.credentials.platformId=player;
    session.activitySessionId=activity;
    session.activityMemberKey=0xAA;
    session.activityCharacterSoid=0x9EAA000100200102ULL;
    auto& c=session.activityContract;
    c.allocated=c.joined=c.identity=c.patchEpoch=c.authoritative=true;
    c.region=c.transitionToken=c.refresh=c.acknowledgement=true;
    c.reportedRegion=3; c.reportedTransitionToken=2;
    record.occupied=record.joined=true;
    record.sessionId=activity;record.memberKey=session.activityMemberKey;
    auto& m=record.membership;
    m.hasIdentity=true;m.identity.memberKey=record.memberKey;
    m.identity.opaqueSoid=session.activityCharacterSoid;
    m.revision=m.acknowledgedRevision=10;m.region.index=3;
    m.hasTransitionToken=true;m.transitionToken=2;
    bool passed=true;
    sync::reset();
    passed &= require(sync::arm(0,1000,true,activity)==Outcome::invalid
        && sync::arm(player,1000,true,0)==Outcome::invalid, "unbound arm rejected");
    passed &= require(sync::revision(player)==0 && !sync::awaiting_client_sync(player),"unknown player ready");
    passed &= require(sync::arm(player,1000,true,activity)==Outcome::armed
        && sync::revision(player)==1 && sync::awaiting_client_sync(player),"private join held");
    passed &= require(!sync::acknowledge_join(other,activity)
        && !sync::acknowledge_join(player,activity+1),"foreign player and stale session cannot release");
    passed &= require(sync::join_contract_ready(session,record),"complete current acknowledgement accepted");
    for (bool* flag : {&c.allocated,&c.joined,&c.identity,&c.patchEpoch,&c.authoritative,
                       &c.region,&c.transitionToken,&c.refresh,&c.acknowledgement}) {
        *flag=false;
        passed &= require(!sync::join_contract_ready(session,record),"partial handshake cannot release");
        *flag=true;
    }
    session.activityJoinedForeignSession=true;
    passed &= require(!sync::join_contract_ready(session,record),"public link cannot release private join");
    session.activityJoinedForeignSession=false;
    session.authenticated=false;
    passed &= require(!sync::join_contract_ready(session,record),"unauthenticated connection rejected");
    session.authenticated=true;
    --m.acknowledgedRevision;
    passed &= require(!sync::join_contract_ready(session,record),"sticky acknowledgement flag cannot accept old revision");
    ++m.acknowledgedRevision;
    ++m.identity.opaqueSoid;
    passed &= require(!sync::join_contract_ready(session,record),"wrong character rejected");
    --m.identity.opaqueSoid;
    ++m.identity.memberKey;
    passed &= require(!sync::join_contract_ready(session,record),"wrong member rejected");
    --m.identity.memberKey;
    ++record.sessionId;
    passed &= require(!sync::join_contract_ready(session,record),"wrong activity rejected");
    --record.sessionId;
    record.restoredDormant=true;
    passed &= require(!sync::join_contract_ready(session,record),"dormant record rejected");
    record.restoredDormant=false;
    ++m.region.index;
    passed &= require(!sync::join_contract_ready(session,record),"old region rejected");
    --m.region.index;
    ++m.transitionToken;
    passed &= require(!sync::join_contract_ready(session,record),"old token rejected");
    --m.transitionToken;
    passed &= require(sync::join_contract_ready(session,record)
        && sync::acknowledge_join(player,activity)
        && !sync::awaiting_client_sync(player) && sync::revision(player)==2,
        "native acknowledgement releases without any account settings report");
    passed &= require(!sync::acknowledge_join(player,activity) && sync::revision(player)==2,
        "duplicate acknowledgement does not churn roster");
    passed &= require(sync::arm(player,2000,false,activity)==Outcome::suppressed,
        "duplicate transition inside window suppressed");
    passed &= require(sync::arm(player,17000,false,activity)==Outcome::armed,
        "later transition held");
    ++m.revision; ++m.transitionToken; c.reportedTransitionToken=m.transitionToken;
    passed &= require(!sync::join_contract_ready(session,record) && sync::awaiting_client_sync(player),
        "new transition cannot reuse previous acknowledged revision");
    m.acknowledgedRevision=m.revision;
    passed &= require(sync::join_contract_ready(session,record) && sync::acknowledge_join(player,activity),
        "current transition acknowledgement releases");
    passed &= require(sync::arm(player,18000,true,activity+1)==Outcome::armed
        && !sync::acknowledge_join(player,activity),"new join rejects previous session acknowledgement");
    passed &= require(sync::arm(other,18000,true,activity)==Outcome::armed
        && sync::acknowledge_join(other,activity) && sync::awaiting_client_sync(player),
        "one player's readiness cannot release another");
    passed &= require(generation::should_advance(3,0x1234,0x1234,true,true,true,false),
        "ready edge publishes changed roster generation");
    passed &= require(!generation::should_advance(3,0x1234,0x1234,false,true,false,false),
        "steady ready roster preserves generation");
    if (!passed) return 1;
    std::puts("PASS: native activity acknowledgement spawn contract");
}
