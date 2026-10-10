#pragma once
#include <array>
#include <cstdio>
#include <cstring>
#include "state/activity/shared/runtime.h"
#include "state/activity/runtime.h"
#include "state/activity/membership/activity_membership_query.h"
#include "state/activity/destination/activity_destination_snapshot.h"
#include "state/activity/entity_slots/transactions/internal.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"
#include "server/bap/encrypted/push/activity/activity_message_push.h"
#include "server/bap/encrypted/push/activity/activity_global_state_push.h"
#include "server/bap/encrypted/push/activity/activity_membership_push.h"
#include "middleware/bap/frame.h"
#include "middleware/encoding/byte_order.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/secure_channel/runtime.h"
#include "server/gameplay/group/group_host.h"
#include "server/gameplay/group/group_host_sessions.h"
#include "server/bap/encrypted/bap_connection_publication.h"
#include "../RestorationServer/src/activity_codec.h"

namespace restoration::tests {
#if defined(TRAVEL54_GROUP_ROUTE_TEST)
bool region_leave_route_fixture(std::uint64_t groupId,std::uint64_t memberKey) noexcept;
#endif
inline bool shared_joined_notification_test(std::uint64_t sessionId,
    const dawn::state::activity::membership::PendingMutation& mutation,
    std::uint32_t expectedJoined,std::size_t expectedConnectionBits=0,
    std::uint32_t expectedViews=0,std::uint32_t expectedOccupied=3) noexcept {
    using namespace dawn;
    namespace encoding=middleware::encoding;
    static server::bap::Scratch scratch{};
    static std::array<std::byte,65536> output{},plain{};
    std::array<std::byte,state::kAesKeySize> key{};
    std::array<std::byte,state::kBapNonceSize> nonce{},initial{};
    server::bap::encrypted::activity_message::ActivityPlan plan{};
    plan.sessionId=sessionId;plan.membershipMutation=mutation;
    std::size_t written{},opened{};
    middleware::bap::OuterFrame frame{};
    if (!server::bap::encrypted::push::activity::append_membership_notification(
            scratch,plan,key,nonce,output,written,false)
        || !middleware::bap::parse_frame(std::span(output).first(written),frame)
        || frame.frameType!=middleware::bap::FrameType::encrypted
        || !middleware::secure_channel::open_frame(key,initial,frame.payload,plain,opened)
        || opened<23) return false;
    const auto clear=std::span<const std::byte>(plain).first(opened);
    if (encoding::read_u16_be(clear.first<2>())!=9 || clear[6]!=std::byte{1}
        || encoding::read_u64_be(clear.subspan<7,8>())!=sessionId
        || encoding::read_u32_be(clear.subspan<15,4>())!=12
        || encoding::read_u32_be(clear.subspan<19,4>())!=opened-23) return false;
    // native layout: one reserved identity adds 362 bits; a second full detail adds 327, with no citizen descriptor
    const std::size_t extra=362+(expectedJoined==3?327:0)+expectedConnectionBits;
    if (opened-23!=(29984+extra+32+7)/8) return false;
    encoding::bits::Reader reader(clear.subspan(23));
    std::uint64_t presence{},joined{},occupied{};
    return reader.skip(29915+extra) && reader.read(1,presence) && presence==1
        && reader.read(32,joined) && joined==expectedJoined
        && reader.read(1,presence) && presence==1
        && reader.read(32,occupied) && occupied==expectedOccupied
        && reader.read(1,presence) && presence==1
        && reader.read(32,occupied) && occupied==expectedViews;
}

inline bool shared_activity_join_self_test(std::uint64_t fixtureSession) noexcept {
    using namespace dawn;
    namespace activity=state::activity;
    namespace shared=activity::shared;
    namespace slots=activity::entity_slots;
    namespace member=activity::membership;
    const auto check=[](bool value,const char* name) noexcept {
        if (!value) std::printf("FAIL: shared activity: %s\n",name);
        return value;
    };
    activity::destination::DestinationSelection destination{};
    if (!check(activity::destination::snapshot(fixtureSession,destination),"fixture")) return false;
    static std::array<state::AccountState,3> accounts{};
    static std::array<activity::ActivityState,3> worlds{};
    static std::array<std::byte,sizeof(state::State)> processBefore{};
    std::memcpy(processBefore.data(),&state::runtime::storage::g_state,processBefore.size());
    const auto configured=state::account_snapshot();
    for (std::size_t i=0;i<3;++i) {
        accounts[i]=configured;
        accounts[i].primarySoid=0x8000100100100100ULL+(i<<32);
        worlds[i]={};
    }
    const std::array<state::AccountRuntimeContext,3> contexts{{
        {&accounts[0],&worlds[0]}, {&accounts[1],&worlds[1]}, {&accounts[2],&worlds[2]}}};
    struct Cleanup {~Cleanup(){server::gameplay::group::reset_host_sessions();}} cleanup;
    const std::array<member::Identity,2> identities{{
        {0x1122334455667788ULL,-1,-1,0x1020304050607080ULL,accounts[0].primarySoid,
            accounts[0].primarySoid+2,0x9080706050403020ULL},
        {0x8877665544332211ULL,-1,-1,0x8070605040302010ULL,accounts[1].primarySoid,
            accounts[1].primarySoid+2,0x2030405060708090ULL}}};
    std::uint64_t id{};
    slots::PendingMutation plan{};
    constexpr auto grant=slots::kSlotCount-8;
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        activity::PendingAllocation allocation{};
        if (!check(activity::prepare_session(destination,id,allocation)
            && activity::prepare_startup_reservations(allocation,identities)
            && activity::commit(allocation)
            && slots::prepare_join(id,identities[0].memberKey,grant,8,plan)
            && slots::commit(plan),"host allocation and partitioned join")) return false;
        member::PendingMutation identity{};
        if (!check(member::prepare_identity(id,identities[0],identity) && member::commit(identity),
            "host identity")) return false;
    }
    static activity::ActivityState hostBefore{};
    hostBefore=worlds[0];
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        member::PendingMutation refresh{};shared::Projection projection{};
        if (!check(member::prepare_refresh(id,0,-1,refresh)
            && shared::project(id,refresh.snapshot,projection)
            && projection.joinedMemberMask==1 && projection.members[1].memberKey==identities[1].memberKey,
            "reserved follower is occupied but not a joined-player detail")) return false;
        if (!check(shared_joined_notification_test(id,refresh,1),
            "production encrypted reservation notification remains local-only")) return false;
    }
    state::AccountRuntimeContext source{};
    if (!check(shared::owner_context(id,source) && source.account==contexts[0].account,
        "canonical owner lookup, not ambient account")) return false;
    {
        const state::ScopedAccountRuntime stranger(contexts[2]);
        if (!check(!shared::prepare_join(source,id,identities[1].memberKey,
            identities[1].opaqueSoid,grant,8,plan),"unreserved account denied")) return false;
    }
    {
        const state::ScopedAccountRuntime follower(contexts[1]);
        for (std::size_t i=0;i<worlds[1].sessions.size();++i) {
            activity::PendingAllocation allocation{};
            std::uint64_t historical{};
            if (!check(activity::prepare_session(destination,historical,allocation)
                && activity::commit(allocation)
                && slots::prepare_join(historical,identities[1].memberKey,grant,8,plan)
                && slots::commit(plan),"fill historical solo table")) return false;
        }
        shared::JoinFailure failure{};
        if (!check(!shared::prepare_join(source,id,identities[1].memberKey,
                identities[1].opaqueSoid,grant,8,plan,&failure)
            && failure==shared::JoinFailure::capacity && !plan.prepared,
            "all current-runtime records refuse reclamation")) return false;
        std::vector<std::byte> durableBefore{},durableAfter{};
        static activity::ActivityState restored{},fullBefore{},resumed{};
        if (!check(activity_codec::encode(worlds[1],durableBefore)
            && activity_codec::decode(durableBefore,accounts[1].primarySoid,restored)
            && activity_codec::encode(restored,durableAfter) && durableBefore==durableAfter
            && std::all_of(restored.sessions.begin(),restored.sessions.end(),
                [](const auto& row){return row.occupied && row.joined && row.restoredDormant;}),
            "old codec bytes unchanged; restored joins are dormant")) return false;
        worlds[1]=restored;
        if (!check(shared::prepare_join(source,id,identities[1].memberKey,
            identities[1].opaqueSoid,grant,8,plan) && plan.targetSlot==0,
            "oldest dormant record selected")) return false;
        auto stale=plan;
        slots::PendingMutation resume{};
        if (!check(slots::prepare_join(worlds[1].sessions[0].sessionId,
                identities[1].memberKey,grant,8,resume) && slots::commit(resume)
            && !worlds[1].sessions[0].restoredDormant && !slots::commit(stale)
            && shared::prepare_join(source,id,identities[1].memberKey,
                identities[1].opaqueSoid,grant,8,plan) && plan.targetSlot==1,
            "rejoined historical session protected and stale plan rejected")) return false;
        resumed=worlds[1];
        auto failingResume=contexts[1];
        failingResume.activityPersistence.save=[](void*,const activity::ActivityState&) noexcept {return false;};
        {
            const state::ScopedAccountRuntime sink(failingResume);
            if (!check(!slots::commit(plan)
                && std::memcmp(&resumed,&worlds[1],sizeof(resumed))==0,
                "failed persistence cannot reclaim a historical victim")) return false;
        }
        worlds[1]=restored;
        fullBefore=worlds[1];
        if (!check(!slots::prepare_join(id,identities[1].memberKey,grant,8,plan),
            "reproduced owner-only join refusal")) return false;
        if (!check(!shared::prepare_join(source,id,identities[1].memberKey+1,
                identities[1].opaqueSoid,grant,8,plan)
            && !shared::prepare_join(source,id,identities[1].memberKey,
                identities[1].opaqueSoid+1,grant,8,plan)
            && !shared::prepare_join(source,id,identities[1].memberKey,
                identities[1].opaqueSoid,grant,9,plan),"wrong key, character, reserve denied")) return false;
        if (!check(shared::prepare_join(source,id,identities[1].memberKey,
            identities[1].opaqueSoid,grant,8,plan),"reserved follower prepares")) return false;
        auto wrong=plan;
        wrong.mask[0]^=std::byte{1};
        if (!check(!slots::commit(wrong),"overlapping/tampered lease rejected")) return false;
        wrong=plan;wrong.sharedRecord.destination.activityIndex^=1;
        if (!check(!slots::commit(wrong),"tampered staged destination rejected")) return false;
        wrong=plan;wrong.peerConnection.hasPeer=true;wrong.peerConnection.peer[0]=std::byte{0xA5};
        if(!check(!slots::commit(wrong),"tampered imported peer metadata rejected"))return false;
        {
            const state::ScopedAccountRuntime stranger(contexts[2]);
            wrong=plan;
            if (!check(!slots::commit(wrong),"cross-context commit rejected")) return false;
        }
        {
            const state::ScopedAccountRuntime host(contexts[0]);
            if (!check(member::republish(id),"stale source fixture")) return false;
            hostBefore=worlds[0];
        }
        if (!check(!slots::commit(plan) && shared::prepare_join(source,id,identities[1].memberKey,
            identities[1].opaqueSoid,grant,8,plan),"source revision guard and reprepare")) return false;
        const auto planBefore=plan;
        auto failing=contexts[1];
        failing.activityPersistence.save=[](void*,const activity::ActivityState&) noexcept {return false;};
        {
            const state::ScopedAccountRuntime sink(failing);
            if (!check(!slots::commit(plan) && !activity::contains(id)
                && std::memcmp(&fullBefore,&worlds[1],sizeof(fullBefore))==0,
                "durable failure leaves no participant")) return false;
        }
        plan=planBefore;
        namespace push=server::bap::encrypted::push::activity;
        middleware::bap::activity_message::global_activity_state::GlobalActivityState global{};
        activity::destination::DestinationSelection planned{};
        if (!check(push::resolve_state(id,global,planned,&plan.sharedRecord.destination)
            && planned.activityIndex==destination.activityIndex
            && planned.descriptorBitLength==destination.descriptorBitLength,
            "precommit join uses canonical destination")) return false;
        static server::bap::Scratch scratch{};
        server::bap::encrypted::activity_message::ActivityPlan wirePlan{};
        wirePlan.sessionId=id; wirePlan.correlation=0x1234ABCD; wirePlan.entitySlotMutation=plan;
        std::array<std::byte,state::kAesKeySize> key{};
        std::array<std::byte,state::kBapNonceSize> nonce{};
        std::array<std::byte,32> tooSmall{};
        std::size_t written=0;
        if (!check(!push::append_join_notifications(scratch,wirePlan,key,nonce,tooSmall,written)
            && written==0 && nonce==std::array<std::byte,state::kBapNonceSize>{}
            && !activity::contains(id),"join staging failure leaves nonce/output/state untouched")) return false;
        static std::array<std::byte,65536> output{};
        if (!check(push::append_join_notifications(scratch,wirePlan,key,nonce,output,written)
            && written>0 && !activity::contains(id),"complete join stages before commit")) return false;
        static activity::ActivityState saved{};
        auto capturing=contexts[1];
        capturing.activityPersistence={&saved,[](void* out,const activity::ActivityState& value) noexcept {
            *static_cast<activity::ActivityState*>(out)=value;return true;}};
        {
            const state::ScopedAccountRuntime sink(capturing);
            if (!check(slots::commit(plan) && !slots::commit(plan) && activity::is_joined(id),
                "follower commit and replay denial")) return false;
        }
        if (!check(std::none_of(saved.sessions.begin(),saved.sessions.end(),
            [id](const auto& row){return row.occupied && row.sessionId==id;}),
            "participant views cannot be resurrected from durable saves")) return false;
        if (!check(!worlds[1].sessions[0].restoredDormant
            && worlds[1].sessions[0].sessionId==id
            && std::equal(worlds[1].sessions.begin()+1,worlds[1].sessions.end(),
                fullBefore.sessions.begin()+1,[](const auto& a,const auto& b){
                    return std::memcmp(&a,&b,sizeof(a))==0;}),
            "private import reclaims only its dormant victim")) return false;
        if (!check(std::memcmp(&hostBefore,&worlds[0],sizeof(hostBefore))==0,
            "host state byte-identical across follower admission")) return false;
        const auto& host=worlds[0].sessions[0];
        const auto& remote=worlds[1].sessions[0];
        if (!check(host.memberKey==identities[0].memberKey && remote.memberKey==identities[1].memberKey
            && slots::transactions::empty(slots::transactions::intersect(host.heldEntitySlots,remote.heldEntitySlots))
            && host.sharedLeaseParts==shared::kPrivateLeaseParts
            && remote.sharedLeaseParts==shared::kPrivateLeaseParts
            && slots::transactions::count(host.heldEntitySlots)==grant/shared::kPrivateLeaseParts
            && slots::transactions::count(remote.heldEntitySlots)==grant*2/shared::kPrivateLeaseParts-grant/shared::kPrivateLeaseParts,
            "distinct keys, complete disjoint private lanes; unused capacity retained for late members")) return false;
        if (!check(slots::prepare_grant(id,100,plan) && slots::transactions::empty(plan.mask)
            && slots::commit(plan),"later grant cannot invade host partition")) return false;
        member::PendingMutation identity{};
        {
            const state::ScopedAccountRuntime hostScope(contexts[0]);
            member::PendingMutation refresh{};shared::Projection projection{};
            if (!check(member::prepare_refresh(id,0,-1,refresh)
                && shared::project(id,refresh.snapshot,projection) && projection.joinedMemberMask==1,
                "admission without type23 cannot create remote player detail")) return false;
        }
        if (!check(member::prepare_identity(id,identities[1],identity) && member::commit(identity)
            && member::prepare_refresh(id,0,-1,identity),"independent follower identity and refresh")) return false;
        if (!check(worlds[0].sessions[0].membership.revision>hostBefore.sessions[0].membership.revision,
            "peer changes advance the host's publication revision")) return false;
        auto spoofed=identities[1];spoofed.accountSoid=identities[0].accountSoid;
        member::PendingMutation denied{};
        if (!check(!member::prepare_identity(id,spoofed,denied),"type23 cannot replace another account identity")) return false;
        shared::Projection projection{};
        if (!check(shared::project(id,identity.snapshot,projection) && projection.localSlot==1
            && projection.members[0].memberKey==identities[0].memberKey
            && projection.members[1].memberKey==identities[1].memberKey
            && projection.hostKey==identities[0].memberKey && projection.joinedMemberMask==3
            && projection.identityWireWordMask==3,
            "canonical membership slots and host authority")) return false;
        if (!check(shared_joined_notification_test(id,identity,3),
            "production follower notification contains both joined details and bits")) return false;
        {
            const state::ScopedAccountRuntime hostScope(contexts[0]);
            member::PendingMutation refresh{};
            if (!check(member::prepare_refresh(id,0,-1,refresh)
                && shared_joined_notification_test(id,refresh,3),
                "production host notification contains both joined details and bits")) return false;
        }
        if (!check(state::account_snapshot().primarySoid==accounts[1].primarySoid,
            "follower account identity retained")) return false;
        bool claimed=false;
        if (!check(server::gameplay::group::activity_host_session(0x7100330000001001ULL,408,id,claimed)==0
            && claimed,"follower advertises canonical public region")) return false;
    }
    namespace group=server::gameplay::group;
    group::allocate_claimed_host_sessions();
    const auto child=group::held_host_session(0x7100330000001001ULL);
    if (!check(child!=0 && worlds[0].sessions[1].sessionId==child,
        "public child allocated under canonical host, not last caller")) return false;
    {
        const state::ScopedAccountRuntime follower(contexts[1]);
        const auto privateBefore=worlds[1].sessions[0];
        static activity::ActivityState publicBefore{};
        publicBefore=worlds[1];
        if (!check(group::source_activity_session_for_host(child)==id
            && group::region_for_host_session(child)==408 && shared::owner_context(child,source)
            && shared::prepare_join(source,child,identities[1].memberKey,identities[1].opaqueSoid,grant,8,plan)
            && slots::commit(plan),"follower-first public join, parent identity and assigned region")) return false;
        member::PendingMutation publicIdentity{};shared::Projection publicProjection{};
        if (!check(member::prepare_identity(child,identities[1],publicIdentity)
            && member::commit(publicIdentity) && member::prepare_refresh(child,0,-1,publicIdentity)
            && shared::project(child,publicIdentity.snapshot,publicProjection)
            && publicProjection.joinedMemberMask==2
            && shared_joined_notification_test(child,publicIdentity,2),
            "follower-first public notification cannot mark unjoined host as a player")) return false;
        if (!check(worlds[1].sessions[1].sessionId==child
            && !worlds[1].sessions[1].restoredDormant
            && std::memcmp(&privateBefore,&worlds[1].sessions[0],sizeof(privateBefore))==0
            && std::equal(worlds[1].sessions.begin()+2,worlds[1].sessions.end(),
                publicBefore.sessions.begin()+2,[](const auto& a,const auto& b){
                    return std::memcmp(&a,&b,sizeof(a))==0;}),
            "full-table public import preserves live private and every other historical record")) return false;
        server::bap::encrypted::transactions::Publication publication{};
        publication.activitySessionId=id;publication.hasActivitySessionBinding=true;publication.activitySessionFromJoin=true;
        static server::bap::Session privateLink{},publicLink{};
        privateLink={};publicLink={};
        server::bap::encrypted::publish_connection_fields(privateLink,publication,{});
        publication.activitySessionId=child;
        server::bap::encrypted::publish_connection_fields(publicLink,publication,{});
        if (!check(!privateLink.activityJoinedForeignSession && publicLink.activityJoinedForeignSession,
            "private follower is not misclassified as a public target")) return false;
    }
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        if (!check(slots::prepare_join(child,identities[0].memberKey,grant,8,plan) && slots::commit(plan),
            "host joins public after follower")) return false;
        member::PendingMutation publicIdentity{};shared::Projection publicProjection{};
        if (!check(member::prepare_identity(child,identities[0],publicIdentity)
            && member::commit(publicIdentity) && member::prepare_refresh(child,0,-1,publicIdentity)
            && shared::project(child,publicIdentity.snapshot,publicProjection)
            && publicProjection.joinedMemberMask==3
            && shared_joined_notification_test(child,publicIdentity,3),
            "public host notification includes both only after host admission and identity")) return false;
        if (!check(slots::transactions::empty(slots::transactions::intersect(
            worlds[0].sessions[1].heldEntitySlots,worlds[1].sessions[1].heldEntitySlots)),
            "public first-arrival ordering preserves disjoint leases")) return false;
    }
    {
        const state::ScopedAccountRuntime stranger(contexts[2]);
        if (!check(group::source_activity_session_for_host(child)==0
            && group::region_for_host_session(child)==group::kUnknownRegion,
            "unrelated account cannot resolve shared public child")) return false;
    }
    std::array<core::protocol::ActivityPeerConnection,2> connections{};
    for(std::size_t i=0;i<2;++i){
        auto& c=connections[i];c.hasFlags=c.hasProxy=c.hasPeer=c.hasAlternate=true;c.flags=0x10;
        c.peer[0]=std::byte{static_cast<unsigned char>(i+17)};c.peer[85]=std::byte{6};c.proxy=c.peer;
        const state::ScopedAccountRuntime scope(contexts[i]);
        member::AuthoritativeUpdate update{};update.peerConnection=c;member::PendingMutation mutation{};
        if(!check(member::prepare_authoritative(id,update,mutation)&&mutation.hasSnapshot
            && member::commit(mutation),"typed connection update commits on own account"))return false;
    }
    for(std::size_t i=0;i<2;++i){
        const state::ScopedAccountRuntime scope(contexts[i]);member::PendingMutation refresh{};
        shared::Projection projection{};
        if(!check(member::prepare_refresh(id,0,-1,refresh)&&shared::project(id,refresh.snapshot,projection)
            &&projection.peerConnections[0]==connections[0]&&projection.peerConnections[1]==connections[1]
            &&shared_joined_notification_test(id,refresh,3,2070,1U<<static_cast<unsigned>(1-i)),
            "real encrypted private host/follower peer fields and distinct view masks"))return false;
        if(!check(member::prepare_refresh(child,0,-1,refresh)&&shared_joined_notification_test(child,refresh,3),
            "public peers without own readiness remain gated"))return false;
    }
    for(std::size_t i=0;i<2;++i) {
        const state::ScopedAccountRuntime scope(contexts[i]);
        member::AuthoritativeUpdate update{};auto& c=update.peerConnection;
        c.hasFlags=c.hasPeer=c.hasProxy=true;c.flags=0x13;
        c.peer[0]=std::byte{0x7f};c.proxy=c.peer;
        member::PendingMutation mutation{};
        if(!check(member::prepare_authoritative(child,update,mutation)&&member::commit(mutation),
            "public native flags and server address fixture"))return false;
    }
    for(std::size_t i=0;i<2;++i) {
        const state::ScopedAccountRuntime scope(contexts[i]);member::PendingMutation refresh{};
        shared::Projection projection{};
        if(!check(member::prepare_refresh(child,0,-1,refresh)
            &&shared::project(child,refresh.snapshot,projection,id)
            &&projection.peerConnections[0].peer==connections[0].peer
            &&projection.peerConnections[1].peer==connections[1].peer
            &&projection.peerConnections[0].flags==0x13 &&projection.peerConnections[1].flags==0x13
            &&shared_joined_notification_test(child,refresh,3,2070,1U<<static_cast<unsigned>(1-i)),
            "PUBLIC encrypted host/follower views use distinct exact PRIVATE endpoints and PUBLIC flags"))return false;
        auto wrong=refresh.snapshot;wrong.identity.joinIdentity^=1;
        if(!check(shared::project(child,wrong,projection,id)
            &&projection.peerConnections[i]==core::protocol::ActivityPeerConnection{},
            "changed player incarnation cannot inherit a prior peer address"))return false;
        wrong=refresh.snapshot;wrong.identity.opaqueSoid^=1;
        if(!check(shared::project(child,wrong,projection,id)
            &&projection.peerConnections[i]==core::protocol::ActivityPeerConnection{},
            "changed character cannot inherit a peer address"))return false;
        if(!check(shared::project(child,refresh.snapshot,projection,child)
            &&projection.peerConnections==decltype(projection.peerConnections){},
            "self source rejected with no server-address fallback"))return false;
        if(!check(shared::project(child,refresh.snapshot,projection,0x123456789ULL)
            &&projection.peerConnections==decltype(projection.peerConnections){},
            "unknown source rejected with no server-address fallback"))return false;
    }
    {
        const auto hostRevision=worlds[0].sessions[1].membership.revision;
        const auto followerRevision=worlds[1].sessions[1].membership.revision;
        const state::ScopedAccountRuntime follower(contexts[1]);
        member::AuthoritativeUpdate update{};update.peerConnection.hasFlags=true;
        member::PendingMutation mutation{};
        if(!check(member::prepare_authoritative(id,update,mutation)&&member::commit(mutation)
            &&worlds[0].sessions[1].membership.revision>hostRevision
            &&worlds[1].sessions[1].membership.revision>followerRevision,
            "PRIVATE readiness withdrawal dirties both PUBLIC publication revisions"))return false;
        {
            const state::ScopedAccountRuntime host(contexts[0]);member::PendingMutation refresh{};
            shared::Projection projection{};
            if(!check(member::prepare_refresh(child,0,-1,refresh)
                &&shared::project(child,refresh.snapshot,projection,id)
                &&projection.peerConnections[1].flags==3
                &&shared_joined_notification_test(child,refresh,3,2070,0),
                "PRIVATE withdrawal preserves PUBLIC flags except ready bit and removes view activation"))return false;
        }
        update.peerConnection.flags=0x10;
        if(!check(member::prepare_authoritative(id,update,mutation)&&member::commit(mutation),
            "restore private readiness fixture"))return false;
    }
    {
        const state::ScopedAccountRuntime isolated(contexts[2]);
        activity::PendingAllocation allocation{};std::uint64_t solo{};
        if(!check(activity::prepare_session(destination,solo,allocation)&&activity::commit(allocation)
            &&slots::prepare_join(solo,0x13579,grant,8,plan),"isolated peer join fixture"))return false;
        plan.peerConnection=connections[0];plan.peerConnectionGuard=connections[0];
        auto wrong=plan;wrong.peerConnection.peer[4]=std::byte{9};
        if(!check(!slots::commit(wrong)&&slots::commit(plan),"join metadata guard and atomic commit"))return false;
        activity::SessionRecord row{};
        if(!check(activity::snapshot_for_runtime(contexts[2],accounts[2].primarySoid,solo,row)
            &&row.membership.peerConnection==connections[0],"join retains initial address before flags-only delta"))return false;
    }
    static std::array<activity::ActivityState,3> beforeDeparture{};
    beforeDeparture=worlds;
    {
        const state::ScopedAccountRuntime owner(contexts[0]);
        member::PendingMutation release{};
        if(!check(member::prepare_reservations(id,member::kStableEpoch,{},
                identities[1].memberKey,release) && member::commit(release),
                "release guest reservation before endpoint rejoin"))return false;
        activity::SessionRecord root{},guest{};
        if(!check(activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,id,root)
            && shared::participant_snapshot(root,accounts[1].primarySoid,guest)
            && root.membership.reservations[0].memberKey==0 && guest.joined
            && guest.membership.hasIdentity,"released reservation has live authenticated guest"))return false;
        std::array<std::uint64_t,32> candidateAccounts{};
        if(!check(shared::participant_accounts_snapshot(root,candidateAccounts)
            && candidateAccounts[0]==accounts[0].primarySoid
            && candidateAccounts[1]==accounts[1].primarySoid,
            "released reservation retains authenticated participant in endpoint candidate set"))return false;
        const auto rootState=worlds[0];const auto guestState=worlds[1];
        const auto empty=[](const auto& values) noexcept {
            return std::all_of(values.begin(),values.end(),[](auto value){return value==0;});
        };
        const auto absent=[&]() noexcept {
            candidateAccounts.fill(0xfeed);
            return shared::participant_accounts_snapshot(root,candidateAccounts)
                && candidateAccounts[0]==accounts[0].primarySoid && candidateAccounts[1]==0;
        };
        worlds[1].sessions[0].joined=false;
        if(!check(absent(),"unjoined guest excluded even with identity"))return false;
        worlds[1]=guestState;worlds[1].sessions[0].membership.hasIdentity=false;
        if(!check(absent(),"guest before type23 excluded"))return false;
        worlds[1]=guestState;++worlds[1].sessions[0].sharedOwnerCreatedRevision;
        if(!check(absent(),"different canonical incarnation excluded"))return false;
        worlds[1]=guestState;worlds[1].sessions[0].sharedOwnerAccountSoid^=1;
        if(!check(absent(),"different canonical owner excluded"))return false;
        worlds[1]=guestState;
        for(unsigned bad=0;bad<5;++bad) {
            auto invalid=root;
            switch(bad) {
            case 0: invalid.sharedOwnerAccountSoid^=1;break;
            case 1: ++invalid.sharedOwnerCreatedRevision;break;
            case 2: ++invalid.createdRevision;break;
            case 3: invalid.sessionId^=0x100000000ULL;break;
            default: invalid.ownerAccountSoid=0;break;
            }
            candidateAccounts.fill(0xfeed);
            if(!check(!shared::participant_accounts_snapshot(invalid,candidateAccounts)
                &&empty(candidateAccounts),"bad/stale root fails closed with cleared result"))return false;
        }
        for(unsigned bad=0;bad<7;++bad) {
            auto& row=worlds[1].sessions[0];
            switch(bad) {
            case 0: row.membership.identity.accountSoid^=1;break;
            case 1: row.membership.identity.memberKey^=1;break;
            case 2: row.sharedMemberSlot=0;break;
            case 3: row.sharedMemberSlot=32;break;
            case 4: row.memberKey=0;break;
            case 5: row.sharedOwnerDetached=true;break;
            default: row.sharedMemberSlot=root.sharedLeaseParts;break;
            }
            candidateAccounts.fill(0xfeed);
            if(!check(!shared::participant_accounts_snapshot(root,candidateAccounts)
                &&empty(candidateAccounts),"invalid live participant cannot authorize endpoint"))return false;
            worlds[1]=guestState;
        }
        const auto thirdAccount=accounts[2];const auto thirdState=worlds[2];
        worlds[2]=guestState;accounts[2]=accounts[1];
        candidateAccounts.fill(0xfeed);
        const bool duplicateRefused=!shared::participant_accounts_snapshot(root,candidateAccounts)
            &&empty(candidateAccounts);
        accounts[2]=thirdAccount;worlds[2]=thirdState;
        if(!check(duplicateRefused,"duplicate canonical account/slot fails closed"))return false;
        if(!check(shared::participant_accounts_snapshot(root,candidateAccounts)
            &&candidateAccounts[1]==accounts[1].primarySoid
            &&std::memcmp(&worlds[0],&rootState,sizeof(rootState))==0
            &&std::memcmp(&worlds[1],&guestState,sizeof(guestState))==0,
            "participant enumeration is read-only and survives repeated lookup"))return false;
        std::printf("PASS: released-reservation activity endpoint rejoin and identity/isolation guards\n");
    }
    worlds=beforeDeparture;
    for(std::size_t leaving=0;leaving<2;++leaving) {
        worlds=beforeDeparture;
        const auto survivor=1-leaving;
        const auto survivorRevision=worlds[survivor].sessions[1].membership.revision;
        if(!check(!shared::disconnect_member(0,identities[leaving].memberKey)
            && !shared::disconnect_member(child,0)
            && !shared::disconnect_member(child,0xdeadbeef)
            && !shared::disconnect_member(0x123456789ULL,identities[leaving].memberKey),
            "unmapped or unadmitted region departure cannot remove a player")) return false;
        const bool departed=
#if defined(TRAVEL54_GROUP_ROUTE_TEST)
            region_leave_route_fixture(0x7100330000001001ULL,identities[leaving].memberKey);
#else
            shared::disconnect_member(child,identities[leaving].memberKey);
#endif
        if(!check(departed
            && worlds[0].sessions[0].joined && worlds[1].sessions[0].joined
            && worlds[survivor].sessions[1].joined
            && worlds[survivor].sessions[1].membership.revision>survivorRevision,
            "region departure is exact/idempotent and publishes surviving activity membership"))return false;
        if(!check(!shared::disconnect_member(child,identities[leaving].memberKey),
            "duplicate region departure is a no-op"))return false;
        const state::ScopedAccountRuntime scope(contexts[survivor]);
        member::PendingMutation refresh{}; shared::Projection projection{};
        const auto mask=std::uint32_t{1}<<survivor;
        if(!check(member::prepare_refresh(child,0,-1,refresh)
            && shared::project(child,refresh.snapshot,projection,id)
            && projection.joinedMemberMask==mask
            && shared_joined_notification_test(child,refresh,mask),
            "real encrypted PUBLIC notification removes departed region body in either direction"))return false;
    }
    worlds=beforeDeparture;
    if(!check(shared::disconnect_member(child,identities[0].memberKey)
        && shared::disconnect_member(child,identities[1].memberKey)
        && !worlds[0].sessions[1].joined && worlds[0].sessions[1].sharedOwnerDetached
        && shared::owner_context(child,source),
        "last region departure removes all players but retains the cached host allocation"))return false;
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        if(!check(slots::prepare_join(child,identities[0].memberKey,grant,8,plan)
            && slots::commit(plan) && worlds[0].sessions[1].joined,
            "return to an empty region reuses the original activity id"))return false;
    }
    {
        const state::ScopedAccountRuntime follower(contexts[1]);
        if(!check(shared::owner_context(child,source)
            && shared::prepare_join(source,child,identities[1].memberKey,identities[1].opaqueSoid,grant,8,plan)
            && slots::commit(plan),"follower can return after both players left the region"))return false;
    }
    worlds=beforeDeparture;
    const auto admitPublicEndpoints=[&]() noexcept {
        const state::ScopedAccountRuntime host(contexts[0]);
        member::PendingMutation refresh{};shared::Projection projection{};
        return member::prepare_refresh(child,0,-1,refresh)
            && shared::project(child,refresh.snapshot,projection,id)
            && projection.peerConnections[0].peer==connections[0].peer
            && projection.peerConnections[1].peer==connections[1].peer;
    };
    for (const auto rejoinSession : {id,child}) {
        worlds=beforeDeparture;
        const state::ScopedAccountRuntime owner(contexts[0]);
        const std::size_t index=rejoinSession==id?0U:1U;
        const auto survivorLease=worlds[1].sessions[index].heldEntitySlots;
        const auto incarnation=worlds[0].sessions[index].createdRevision;
        for (unsigned attempt=0;attempt<3;++attempt) {
            shared::disconnect(contexts[0],rejoinSession);
            if(!check(worlds[0].sessions[index].sharedOwnerDetached
                && !worlds[0].sessions[index].joined
                && worlds[0].sessions[index].memberKey==identities[0].memberKey,
                "retained owner keeps its authenticated member key"))return false;
            if(!check(!slots::prepare_join(rejoinSession,identities[0].memberKey^1,grant,8,plan),
                "detached owner refuses a different member key"))return false;
            if(!check(slots::prepare_join(rejoinSession,identities[0].memberKey,grant,8,plan),
                "same-owner retained activity prepares rejoin"))return false;
            auto changed=plan;changed.memberKey^=1;
            if(!check(!slots::commit(changed),"retained rejoin rejects tampered request key"))return false;
            const auto anchor=worlds[0].sessions[index];
            for (unsigned bad=0;bad<7;++bad) {
                auto& row=worlds[0].sessions[index];
                row=anchor;
                switch(bad) {
                case 0: row.sharedOwnerDetached=false;break;
                case 1: ++row.sharedOwnerCreatedRevision;break;
                case 2: row.sharedMemberSlot=1;break;
                case 3: row.heldEntitySlots[0]=std::byte{1};break;
                case 4: row.membership.hasIdentity=false;break;
                case 5: row.membership.identity.accountSoid^=1;break;
                default: row.serverEntitySlots[0]^=std::byte{1};break;
                }
                auto invalid=plan;
                const bool refused=!slots::commit(invalid);
                row=anchor;
                if(!check(refused,"retained rejoin refuses inconsistent anchor/identity/lease"))return false;
            }
            if(!check(slots::commit(plan),"retained canonical owner rejoin commits"))return false;
            if(!check(!slots::commit(plan),"retained rejoin cannot replay consumed plan"))return false;
            member::PendingMutation refresh{};shared::Projection projection{};
            if(!check(worlds[0].sessions[index].joined
                && !worlds[0].sessions[index].sharedOwnerDetached
                && worlds[0].sessions[index].createdRevision==incarnation
                && worlds[1].sessions[index].joined
                && worlds[1].sessions[index].heldEntitySlots==survivorLease
                && slots::transactions::empty(slots::transactions::intersect(
                    worlds[0].sessions[index].heldEntitySlots,survivorLease))
                && member::prepare_refresh(rejoinSession,0,-1,refresh)
                && shared::project(rejoinSession,refresh.snapshot,projection)
                && projection.joinedMemberMask==3,
                "rejoined owner preserves survivor identity and disjoint leases"))return false;
        }
    }
    for (bool privateFirst : {false,true}) {
        worlds=beforeDeparture;
        if(!check(admitPublicEndpoints(),"rebuild isolated public endpoint binding"))return false;
        shared::disconnect(contexts[0],privateFirst?id:child);
        shared::disconnect(contexts[0],privateFirst?child:id);
        const auto afterClose=worlds[0].stateRevision;
        shared::disconnect(contexts[0],child);
        const state::ScopedAccountRuntime follower(contexts[1]);
        member::PendingMutation refresh{};shared::Projection projection{};
        if(!check(activity::is_joined(child) && activity::is_joined(id)
            && worlds[1].sessions[1].heldEntitySlots==beforeDeparture[1].sessions[1].heldEntitySlots
            && worlds[1].sessions[1].sharedMemberSlot==1
            && worlds[1].sessions[1].sharedOwnerCreatedRevision==beforeDeparture[1].sessions[1].sharedOwnerCreatedRevision
            && worlds[1].sessions[1].membership.revision>beforeDeparture[1].sessions[1].membership.revision
            && worlds[1].sessions[1].membership.acknowledgedRevision==0
            && worlds[0].sessions[1].sharedOwnerDetached && !worlds[0].sessions[1].joined
            && slots::transactions::empty(worlds[0].sessions[1].heldEntitySlots)
            && worlds[0].stateRevision==afterClose
            && group::source_activity_session_for_host(child)==id
            && group::region_for_host_session(child)==408
            && member::prepare_refresh(child,0,-1,refresh)
            && shared::project(child,refresh.snapshot,projection,id)
            && projection.joinedMemberMask==2 && projection.localSlot==1
            && projection.peerConnections[0]==core::protocol::ActivityPeerConnection{}
            && projection.peerConnections[1].peer==connections[1].peer
            && projection.hostKey==identities[1].memberKey,
            "owner departure preserves public/private survivor, stable slots/leases and remaining coordinator"))return false;
        shared::disconnect(contexts[1],id);
        if(!check(activity::is_joined(child)
            && group::source_activity_session_for_host(child)==id
            && group::region_for_host_session(child)==408
            && member::prepare_refresh(child,0,-1,refresh)
            && shared::project(child,refresh.snapshot,projection,id)
            && projection.peerConnections[1].peer==connections[1].peer
            && projection.peerConnections[1].flags==0x13,
            "admitted public member retains authenticated transport after private closure"))return false;
        auto wrong=refresh.snapshot;wrong.identity.joinIdentity^=1;
        if(!check(shared::project(child,wrong,projection,id)
            && projection.peerConnections[1]==core::protocol::ActivityPeerConnection{},
            "detached public transport still rejects changed player incarnation"))return false;
        shared::disconnect(contexts[1],child);
        if(!check(!activity::contains(child) && !shared::owner_context(child,source)
            && !shared::owner_context(id,source),"last departures release canonical anchors"))return false;
    }
    worlds=beforeDeparture;
    if(!check(admitPublicEndpoints(),"restore public endpoint fixture"))return false;
    shared::disconnect(contexts[1],id);
    {
        const state::ScopedAccountRuntime host(contexts[0]);member::PendingMutation refresh{};
        shared::Projection projection{};
        if(!check(member::prepare_refresh(child,0,-1,refresh)
            &&shared::project(child,refresh.snapshot,projection,id)
            &&projection.peerConnections[1].peer==connections[1].peer
            &&shared_joined_notification_test(child,refresh,3,2070,2),
            "PRIVATE disconnect does not revoke an independently joined PUBLIC participant"))return false;
    }
    shared::disconnect(contexts[1],child);
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        member::PendingMutation refresh{};shared::Projection projection{};
        if (!check(member::prepare_refresh(id,0,-1,refresh)
            && shared::project(id,refresh.snapshot,projection) && projection.joinedMemberMask==1,
            "disconnected follower loses its joined-player bit")) return false;
    }
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        if (!check(activity::contains(child),"follower disconnect preserves host child")) return false;
    }
    shared::disconnect(contexts[0],id);
    {
        const state::ScopedAccountRuntime follower(contexts[1]);
        if (!check(!activity::contains(id) && !shared::owner_context(id,source),
            "host disconnect retires all private participant views")) return false;
    }
    const bool ok=std::memcmp(processBefore.data(),&state::runtime::storage::g_state,processBefore.size())==0;
    if (ok) {
        std::puts("PASS: real shared admission, refusal reproduction, account/revision/replay guards, disjoint leases, canonical slots, destination and staging");
        std::puts("PASS: full restored solo table supports private/public imports; live and resumed records protected; persistence/stale rollback; codec bytes unchanged");
        std::puts("PASS: joined-player detail projection excludes reservations/unidentified/disconnected peers; encrypted host/follower svc9 masks and sizes verified");
        std::puts("PASS: PUBLIC peer views use exact PRIVATE endpoints with account/character/incarnation guards, own PUBLIC readiness and dependency withdrawal");
        std::puts("PASS: owner departure in both close orders preserves survivor/slots/leases, republishes removal, retains admitted PUBLIC transport and cleans last anchors");
    }
    return check(ok,"process owner untouched");
}
}
