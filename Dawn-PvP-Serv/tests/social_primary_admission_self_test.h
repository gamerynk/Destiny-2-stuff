#pragma once
#include "public_independent_arrival_self_test.h"
#include "state/activity/shared/social_admission.h"
#include "server/bap/encrypted/push/activity/activity_keepalive_push.h"
#include "social_search_self_test.h"
#include "social_native_handoff_self_test.h"
#include "middleware/social/rendezvous_batch.h"

namespace restoration::tests {
inline bool social_primary_case(std::uint64_t baseline,std::string_view name,std::int16_t activityIndex,
    std::uint32_t activityHash) noexcept {
    using namespace dawn;
    namespace a=state::activity;
    const auto check=[](bool ok,const char* label){if(!ok)std::printf("FAIL: social primary: %s\n",label);return ok;};
    static std::array<state::AccountState,3> accounts{};
    static std::array<a::ActivityState,3> worlds{};
    static std::array<PublicRegionTestSink,3> sinks{};
    static std::array<state::AccountRuntimeContext,3> contexts{};
    std::array<a::membership::Identity,3> identities{},reserved{};
    a::destination::DestinationSelection destination{};
    if(!a::destination::snapshot(baseline,destination))return false;
    destination.packageName={};destination.packageNameLength=static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(),name.end(),destination.packageName.begin());destination.activityIndex=activityIndex;
    destination.hasDescriptorName=false;destination.descriptorBitLength=0;
    const auto configured=state::account_snapshot();
    for(std::size_t i=0;i<3;++i) {
        accounts[i]=configured;accounts[i].primarySoid^=std::uint64_t{0x30+i}<<20;
        worlds[i]={};sinks[i]={};contexts[i]={&accounts[i],&worlds[i],{}, {&sinks[i],PublicRegionTestSink::save}};
        identities[i].memberKey=0xAC99000000000001ULL+i;identities[i].smallOpaque=-1;
        identities[i].accountSoid=accounts[i].primarySoid;identities[i].opaqueSoid=accounts[i].primarySoid+1;
        identities[i].joinIdentity=0x9812345678000001ULL+i;
        identities[i].secondaryOpaque=0xABCD123400000001ULL;
        reserved[i]=a::shared::as_reservation(identities[i]);
    }
    const auto allocate=[&](std::size_t i,a::PendingAllocation& plan) {
        const state::ScopedAccountRuntime scope(contexts[i]);std::uint64_t id{};
        return a::prepare_session(destination,id,plan)
            && a::prepare_startup_reservations(plan,std::span(&reserved[i],1))
            && a::shared::prepare_social_allocation(plan);
    };
    const auto join=[&](std::size_t i,std::uint64_t id) {
        const state::ScopedAccountRuntime scope(contexts[i]);a::entity_slots::PendingMutation plan{};
        a::membership::PendingMutation identity{};state::AccountRuntimeContext root{};
        if(!a::shared::social_join_authorized(id,identities[i].memberKey,identities[i].opaqueSoid))return false;
        if(!a::entity_slots::prepare_join(id,identities[i].memberKey,128,8,plan)
            && (!a::shared::owner_context(id,root) || !a::shared::prepare_join(root,id,identities[i].memberKey,
                identities[i].opaqueSoid,128,8,plan)))return false;
        plan.peerConnection.hasPeer=plan.peerConnection.hasProxy=plan.peerConnection.hasFlags=true;
        plan.peerConnection.flags=0x10;plan.peerConnection.peer[0]=std::byte{static_cast<unsigned char>(i+1)};
        plan.peerConnection.peer.back()=std::byte{6};plan.peerConnectionGuard=plan.peerConnection;
        return a::entity_slots::commit(plan)
            && a::membership::prepare_identity(id,identities[i],identity) && a::membership::commit(identity);
    };
    const auto admitted=[&](std::size_t i,std::uint64_t id) {
        const state::ScopedAccountRuntime scope(contexts[i]);
        return a::shared::social_join_authorized(id,identities[i].memberKey,identities[i].opaqueSoid);
    };
    a::PendingAllocation first{},racing{};
    if(!check(allocate(0,first) && allocate(1,racing) && first.socialSessionId==0 && racing.socialSessionId==0,
        "two precommit allocations"))return false;
    const auto host=first.sessionId;
    {const state::ScopedAccountRuntime scope(contexts[0]);if(!check(a::commit(first),"first social activity"))return false;}
    {const state::ScopedAccountRuntime scope(contexts[1]);if(!check(!a::commit(racing),"concurrent first allocation cannot split instance"))return false;}
    if(!check(join(0,host),"native primary owner joins, no PUBLIC target"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        a::membership::PendingMutation pending{};
        if(!check(a::membership::prepare_reservations(host,a::membership::kStableEpoch,
            std::span(&reserved[1],1),0,pending) && pending.changesState,
            "matchmade visitor reservation prepared"))return false;
        const auto storedRevision=worlds[0].stateRevision;
        if(!check(shared_joined_notification_test(host,pending,1),
            "precommit type12 includes pending reservation, occupied=3 joined=1"))return false;
        a::shared::Projection projected{};
        if(!check(a::shared::project(host,pending.snapshot,projected)
            && projected.members[1].memberKey==reserved[1].memberKey
            && projected.members[1].joinIdentity==reserved[1].joinIdentity
            && projected.joinedMemberMask==1 && projected.identityWireWordMask==1
            && worlds[0].stateRevision==storedRevision,
            "reservation projection preserves byte order without joining or committing visitor"))return false;
        sinks[0].reject=true;auto rejected=pending;
        if(!check(!a::membership::commit(rejected) && worlds[0].stateRevision==storedRevision,
            "failed reservation persistence cannot publish state"))return false;
        sinks[0].reject=false;
        const auto revision=pending.snapshot.revision;
        if(!check(a::membership::commit(pending),"commit matchmade visitor reservation"))return false;
        a::membership::PendingMutation refresh{},ack{};
        if(!check(a::membership::prepare_refresh(host,0,-1,refresh)
            && refresh.snapshot.revision==revision && shared_joined_notification_test(host,refresh,1)
            && a::membership::prepare_acknowledgement(host,revision,ack)
            && a::membership::commit(ack) && a::membership::acknowledged(host),
            "acknowledged revision carries the same occupied table before and after commit"))return false;
        std::puts("PASS: native matchmade social reservation: encrypted precommit admission and acknowledgement");
    }
    a::PendingAllocation second{};
    if(!check(allocate(1,second) && second.socialSessionId==host,"unrelated account selects same primary"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        auto tampered=second;tampered.startupOwner.opaqueSoid++;
        if(!check(!a::commit(tampered),"tampered identity refused"))return false;
        sinks[0].reject=true;auto failed=second;
        if(!check(!a::commit(failed),"failed canonical save refuses admission"))return false;
        sinks[0].reject=false;if(!check(a::commit(second),"commit visitor admission"))return false;
        if(!check(!a::shared::social_join_authorized(host,identities[2].memberKey,identities[2].opaqueSoid),"unreserved identity denied"))return false;
    }
    if(!check(!a::shared::public_peers(accounts[0].primarySoid,accounts[1].primarySoid),"reservation is not live signaling"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);a::membership::PendingMutation release{};
        if(!check(a::membership::prepare_reservations(host,a::membership::kStableEpoch,{},
            identities[1].memberKey,release) && !release.changesState && a::membership::commit(release),
            "private-posse release cannot revoke a pending social admission"))return false;
        if(!a::membership::prepare_reservations(host,a::membership::kStableEpoch,{},
            identities[1].memberKey,release))return false;
        for(std::size_t r=0;r<release.reservationsGuard.size();++r)
            if(release.reservationsGuard[r].memberKey==identities[1].memberKey) {
                release.reservationsGuard[r]={};release.snapshot.reservations[r]={};
            }
        ++release.snapshot.revision;release.changesState=true;
        if(!check(!a::membership::commit(release),"forged matching social release guards cannot delete admission"))return false;
    }
    if(!check(join(1,host),"independent primary visitor join"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        a::membership::PendingMutation pending{};a::shared::Projection projected{};
        if(!check(a::membership::prepare_reservations(host,a::membership::kStableEpoch,
            std::span(&reserved[2],1),0,pending) && pending.changesState
            && a::shared::project(host,pending.snapshot,projected)
            && projected.members[2].memberKey==reserved[2].memberKey
            && projected.members[1].joinIdentity==identities[1].joinIdentity
            && projected.joinedMemberMask==3 && projected.identityWireWordMask==3,
            "new reservation preserves joined peer details and is not promoted to joined"))return false;
        if(!check(a::membership::commit(pending),"third reservation committed"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        a::membership::PendingMutation refresh{};a::shared::Projection projected{};
        if(!a::membership::prepare_refresh(host,0,-1,refresh))return false;
        refresh.snapshot.reservations={};
        if(!check(a::shared::project(host,refresh.snapshot,projected)
            && projected.members[0].memberKey==identities[0].memberKey
            && projected.members[1].joinIdentity==identities[1].joinIdentity
            && projected.members[2].memberKey==reserved[2].memberKey
            && projected.joinedMemberMask==3,
            "stale participant reservation copy cannot erase canonical admissions"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[0]);a::membership::PendingMutation release{};
        if(!check(a::membership::prepare_reservations(host,a::membership::kStableEpoch,{},
            identities[1].memberKey,release) && !release.changesState && a::membership::commit(release),
            "private-posse release cannot revoke a joined social participant"))return false;
    }
    if(!check(a::shared::public_peers(accounts[0].primarySoid,accounts[1].primarySoid)
        && a::shared::public_peers(accounts[1].primarySoid,accounts[0].primarySoid),"bidirectional social signaling without party merge"))return false;
    a::SessionRecord one{},two{};
    if(!check(a::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,host,one)
        && a::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,two)
        && one.socialWorld && two.socialWorld && !one.publicWorld && !two.publicWorld
        && one.sharedLeaseParts==32 && two.sharedLeaseParts==32,"native PRIVATE social projections"))return false;
    for(std::size_t k=0;k<one.heldEntitySlots.size();++k)
        if(!check((one.heldEntitySlots[k]&two.heldEntitySlots[k])==std::byte{0},"disjoint actor leases"))return false;
    if(name=="city_tower_social_d2") {
        const state::ScopedAccountRuntime scope(contexts[1]);
        a::membership::AuthoritativeUpdate update{};a::membership::PendingMutation change{};
        update.hasRegion=true;update.region.index=48;update.region.hash=0xAB28899E;
        update.hasTransitionToken=true;update.transitionToken=1;
        if(!check(a::membership::prepare_authoritative(host,update,change)
            && a::membership::commit(change),"visitor reports native Tower arrival"))return false;
        static server::bap::Session connection{};connection={};
        static server::bap::Scratch scratch{};scratch={};
        static std::array<std::byte,client::network::kBapFrameCapacity> frame{};
        connection.activitySessionId=host;connection.activityMemberKey=identities[1].memberKey;
        connection.activityCharacterSoid=identities[1].opaqueSoid;
        const auto push=[&](std::size_t capacity) {
            std::size_t written{};bool touches{};
            return server::bap::encrypted::push::activity::consume_activity_keepalive(
                connection,scratch,std::span(frame).first(capacity),written,touches);
        };
        if(!check(push(frame.size()) && connection.activityMembershipBaselineRegion==48
            && connection.activityAdvertisedRegion==-1,"Tower baseline delivered before membership"))return false;
        connection.activityKeepaliveDueTick=0;
        const auto nonce=connection.sendNonce;
        if(!check(!push(1) && connection.activityAdvertisedRegion==-1 && connection.sendNonce==nonce,
            "discarded social frame preserves region trigger and nonce"))return false;
        if(!check(push(frame.size()) && connection.activityAdvertisedRegion==48,
            "delivered social membership completes region without Citizen advertisement"))return false;
        connection.activityKeepaliveDueTick=~std::uint64_t{0};
        connection.activityRosterDueTick=~std::uint64_t{0};
        for(unsigned poll=0;poll<100;++poll)
            if(!check(!push(frame.size()),"unchanged Tower region does not flood each service poll"))return false;
        if(!check(a::membership::republish(host),"later social membership revision"))return false;
        connection.activityKeepaliveDueTick=0;
        if(!check(push(frame.size()),"later social revision is not suppressed"))return false;
        update.region.index=56;update.transitionToken=2;
        if(!check(a::membership::prepare_authoritative(host,update,change)
            && a::membership::commit(change) && push(frame.size())
            && connection.activityMembershipBaselineRegion==56,
            "real Tower region change opens new publication"))return false;
        connection.activityKeepaliveDueTick=0;
        if(!check(push(frame.size()) && connection.activityAdvertisedRegion==56,
            "second Tower region completes publication"))return false;
        std::puts("PASS: social keepalive real encrypted publication, discarded frame retry, 100 quiet polls and later revisions");
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);a::membership::PendingMutation refresh{};a::shared::Projection view{};
        if(!a::membership::prepare_refresh(host,0,-1,refresh))return false;
        refresh.snapshot.spawn.opaqueValue=12345;refresh.snapshot.teleport.sliceSetIndex=56;
        if(!check(a::shared::project(host,refresh.snapshot,view) && view.joinedMemberMask==3
            && view.peerConnections[0].peer[0]==std::byte{1} && view.peerConnections[1].peer[0]==std::byte{2}
            && view.spawn.opaqueValue==12345 && view.teleport.sliceSetIndex==56,
            "distinct direct endpoints and visitor-owned spawn/teleport"))return false;
    }
    if(!social_search_self_test(contexts[0],contexts[2],host,activityHash))return false;
    if(!social_native_handoff_self_test(contexts[0],contexts[1],host,activityHash)
        || !social_native_handoff_self_test(contexts[1],contexts[0],host,activityHash))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        auto outgoing=identities[1];outgoing.secondaryOpaque^=0x100;
        a::membership::PendingMutation change{};
        if(!check(a::membership::prepare_identity(host,outgoing,change)
            && a::membership::commit(change),"RUN93 outgoing native FTID changed before EOF"))return false;
    }
    a::membership::PendingMutation beforeDeparture{};
    {const state::ScopedAccountRuntime scope(contexts[0]);
        if(!a::membership::prepare_refresh(host,0,-1,beforeDeparture))return false;}
    a::shared::disconnect(contexts[1],host);
    if(!check(!a::shared::public_peers(accounts[0].primarySoid,accounts[1].primarySoid),"departure revokes signaling"))return false;
    {
        // RUN93: the visitor's ActivityClient EOF precedes its posse leave ack, and C171F0/C17A04 sees
        // an identity-only reservation as an existing peer; departure must publish absence, not just clear the mask
        const state::ScopedAccountRuntime scope(contexts[0]);
        a::membership::PendingMutation refresh{};a::shared::Projection view{};
        if(!check(a::membership::prepare_refresh(host,0,-1,refresh)
            && a::shared::project(host,refresh.snapshot,view)
            && view.members[1].memberKey==0 && view.joinedMemberMask==1
            && view.members[0].memberKey==identities[0].memberKey
            && view.members[2].memberKey==reserved[2].memberKey,
            "RUN93 departed visitor has no stale occupied reservation"))return false;
        if(!check(shared_joined_notification_test(host,refresh,1,0,0,5),
            "RUN93 encrypted type12 removes only departed slot; pending stranger remains"))return false;
        if(!check(!admitted(1,host),
            "RUN93 old admission cannot silently reopen without fresh authorized startup"))return false;
        if(!check(!a::membership::commit(beforeDeparture),
            "RUN93 pre-departure transaction cannot restore retired reservation"))return false;
        const auto revision=worlds[0].stateRevision;
        a::shared::disconnect(contexts[1],host);
        if(!check(worlds[0].stateRevision==revision,"RUN93 duplicate EOF does not change survivor"))return false;
        std::puts("PASS: social departure: actual EOF removes stale reservation from encrypted membership, preserves pending admission and survivor");
    }
    // RUN86: leave/solo return kept machine and player identity; only the FTID (type23 field6) changed
    identities[1].secondaryOpaque=0xABCD123400000002ULL;
    reserved[1]=a::shared::as_reservation(identities[1]);
    if(!check(allocate(1,second) && second.socialSessionId==host,"first-attempt node rejoin selects survivor"))return false;
    {const state::ScopedAccountRuntime scope(contexts[1]);if(!a::commit(second))return false;}
    if(!check(join(1,host),"fresh primary incarnation rejoins"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        a::membership::PendingMutation release{},refresh{};a::shared::Projection view{};
        const auto revision=worlds[0].stateRevision;
        a::shared::disconnect(contexts[1],host,two.joinedRevision);
        if(!check(worlds[0].stateRevision==revision
            && a::membership::prepare_refresh(host,0,-1,refresh)
            && a::shared::project(host,refresh.snapshot,view) && view.joinedMemberMask==3,
            "RUN93 stale old-incarnation EOF cannot retire successful same-key return"))return false;
        if(!check(a::membership::prepare_reservations(host,a::membership::kStableEpoch,{},
            identities[1].memberKey,release) && !release.changesState && a::membership::commit(release)
            && admitted(1,host),
            "RUN93 delayed type14 still cannot revoke a newly joined social participant"))return false;
        std::puts("PASS: social departure: changed FTID, stale transaction/EOF guards, same-key fresh return and protected type14");
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        a::membership::AuthoritativeUpdate update{};a::membership::PendingMutation change{},refresh{};
        update.hasRegion=true;update.region.index=48;update.region.hash=0xAB28899E;
        update.hasTransitionToken=true;update.transitionToken=1;
        a::shared::Projection view{};
        if(!check(a::membership::prepare_authoritative(host,update,change)
            && a::membership::commit(change) && a::membership::prepare_refresh(host,0,-1,refresh)
            && a::shared::project(host,refresh.snapshot,view)
            && view.joinedMemberMask==3 && view.localSlot==1
            && view.members[1].joinIdentity==identities[1].joinIdentity
            && view.members[1].secondaryOpaque==identities[1].secondaryOpaque
            && view.members[0].secondaryOpaque==identities[0].secondaryOpaque
            && view.peerConnections[0].peer[0]==std::byte{1}
            && view.peerConnections[1].peer[0]==std::byte{2},
            "same-player solo return preserves independent FTID and both endpoints"))return false;
        static server::bap::Session connection{};connection={};
        static server::bap::Scratch scratch{};scratch={};
        static std::array<std::byte,client::network::kBapFrameCapacity> frame{};
        connection.activitySessionId=host;connection.activityMemberKey=identities[1].memberKey;
        connection.activityCharacterSoid=identities[1].opaqueSoid;
        std::size_t written{};bool touches{};
        const auto push=[&] {
            return server::bap::encrypted::push::activity::consume_activity_keepalive(
                connection,scratch,frame,written,touches);
        };
        if(!check(push() && connection.activityMembershipBaselineRegion==48,
            "same-player return delivers initial baseline"))return false;
        connection.activityKeepaliveDueTick=0;
        auto receiveNonce=connection.sendNonce;
        if(!check(push() && connection.activityAdvertisedRegion==48,
            "same-player return sends encrypted primary membership"))return false;
        bool membershipReceived=false;std::size_t consumed{};
        while(consumed<written) {
            middleware::bap::OuterFrame outer{};
            if(!middleware::bap::parse_frame(std::span(frame).subspan(consumed,written-consumed),outer))return false;
            std::array<std::byte,client::network::kBapFrameCapacity> clear{};std::size_t length{};
            if(!middleware::secure_channel::open_frame(connection.credentials.bap.sessionKey,
                receiveNonce,outer.payload,clear,length))return false;
            middleware::secure_channel::advance_nonce(receiveNonce);
            if(length>=25 && middleware::encoding::read_u16_be(std::span(clear).first<2>())==9
                && middleware::encoding::read_u32_be(std::span(clear).subspan<15,4>())==12) {
                membershipReceived=true;
            }
            consumed+=6+outer.payload.size();
        }
        if(!check(membershipReceived,"same-player return recipient decrypts type12"))return false;
        a::membership::PendingMutation ack{};
        if(!check(a::membership::prepare_acknowledgement(host,refresh.snapshot.revision,ack)
            && a::membership::commit(ack) && a::membership::acknowledged(host),
            "same-player return accepts current membership acknowledgement"))return false;
        std::puts("PASS: social same-player return: independent FTID, endpoint projection, encrypted membership and acknowledgement");
    }
    a::shared::disconnect(contexts[0],host);
    if(!check(a::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,two) && two.joined,"survivor stays after original player leaves"))return false;
    identities[0].joinIdentity+=100;identities[0].memberKey+=100;reserved[0]=a::shared::as_reservation(identities[0]);
    if(!check(allocate(0,first) && first.socialSessionId==host,"original allocator rejoins survivor with fresh identity"))return false;
    {const state::ScopedAccountRuntime scope(contexts[0]);if(!a::commit(first))return false;}
    if(!check(join(0,host),"original allocator reopens only slot zero"))return false;
    a::shared::disconnect(contexts[0],host);
    a::shared::disconnect(contexts[1],host);
    if(!check(!a::shared::social_session(host),"last occupant retires social instance"))return false;
    std::printf("PASS: social primary PRIVATE-region admission, races, departure/rejoin: %.*s\n",static_cast<int>(name.size()),name.data());return true;
}
inline bool social_party_case(std::uint64_t baseline,bool visitorFirst) noexcept {
    using namespace dawn;
    namespace a=state::activity;
    namespace realtime=middleware::social::realtime;
    const auto check=[](bool ok,const char* step){if(!ok)std::printf("FAIL: mixed social: %s\n",step);return ok;};
    static std::array<state::AccountState,3> accounts{};
    static std::array<a::ActivityState,3> worlds{};
    static std::array<PublicRegionTestSink,3> sinks{};
    static std::array<state::AccountRuntimeContext,3> contexts{};
    std::array<a::membership::Identity,3> identities{},reserved{};
    a::destination::DestinationSelection destination{};
    if(!a::destination::snapshot(baseline,destination))return false;
    constexpr std::string_view name="city_tower_social_d2";
    destination.packageName={};destination.packageNameLength=static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(),name.end(),destination.packageName.begin());destination.activityIndex=20;
    destination.hasDescriptorName=false;destination.descriptorBitLength=0;
    const auto configured=state::account_snapshot();
    for(std::size_t i=0;i<3;++i) {
        accounts[i]=configured;accounts[i].primarySoid^=std::uint64_t{0x40+i}<<20;
        worlds[i]={};sinks[i]={};contexts[i]={&accounts[i],&worlds[i],{}, {&sinks[i],PublicRegionTestSink::save}};
        auto& id=identities[i];id.memberKey=0xAE00000000000001ULL+i;id.smallOpaque=-1;
        id.accountSoid=accounts[i].primarySoid;id.opaqueSoid=id.accountSoid+1;id.joinIdentity=0x7788000000000001ULL+i;
        reserved[i]=a::shared::as_reservation(id);
    }
    const std::array<std::size_t,3> order=visitorFirst?std::array<std::size_t,3>{2,0,1}:std::array<std::size_t,3>{0,1,2};
    std::uint64_t host{};
    for(const auto i:order) {
        const state::ScopedAccountRuntime scope(contexts[i]);
        if(i!=1) {
            a::PendingAllocation plan{};std::uint64_t tentative{};
            const auto party=i==0?std::span(reserved).first(2):std::span(reserved).subspan(2,1);
            if(!check(a::prepare_session(destination,tentative,plan) && a::prepare_startup_reservations(plan,party)
                && a::shared::prepare_social_allocation(plan),"whole arriving party admitted"))return false;
            const auto selected=plan.socialSessionId?plan.socialSessionId:plan.sessionId;
            if(host!=0 && !check(host==selected,"party and visitor select same primary"))return false;
            host=selected;
            if(!check(a::commit(plan),"social allocation commit"))return false;
        }
        a::entity_slots::PendingMutation plan{};a::membership::PendingMutation identity{};
        state::AccountRuntimeContext owner{};
        if(!check(a::shared::social_join_authorized(host,identities[i].memberKey,identities[i].opaqueSoid)
            && (a::entity_slots::prepare_join(host,identities[i].memberKey,128,8,plan)
                || (a::shared::owner_context(host,owner) && a::shared::prepare_join(owner,host,identities[i].memberKey,
                    identities[i].opaqueSoid,128,8,plan))),"independent visitor and reserved follower authorized"))return false;
        plan.peerConnection.hasPeer=plan.peerConnection.hasProxy=plan.peerConnection.hasFlags=true;
        plan.peerConnection.flags=0x10;plan.peerConnection.peer[0]=std::byte{static_cast<unsigned char>(i+1)};
        plan.peerConnection.peer.back()=std::byte{6};plan.peerConnectionGuard=plan.peerConnection;
        if(!check(a::entity_slots::commit(plan) && a::membership::prepare_identity(host,identities[i],identity)
            && a::membership::commit(identity),"primary join and identity"))return false;
    }
    std::array<a::SessionRecord,3> rows{};
    for(std::size_t i=0;i<3;++i) {
        const state::ScopedAccountRuntime scope(contexts[i]);a::membership::PendingMutation refresh{};a::shared::Projection view{};
        if(!check(a::snapshot_for_runtime(contexts[i],accounts[i].primarySoid,host,rows[i])
            && rows[i].socialWorld && !rows[i].publicWorld && rows[i].joined
            && a::membership::prepare_refresh(host,0,-1,refresh) && a::shared::project(host,refresh.snapshot,view)
            && view.joinedMemberMask==7,"all three native primary peers, no synthetic PUBLIC region"))return false;
        for(std::size_t j=0;j<i;++j)for(std::size_t k=0;k<rows[i].heldEntitySlots.size();++k)
            if(!check((rows[i].heldEntitySlots[k]&rows[j].heldEntitySlots[k])==std::byte{0},"disjoint actor slots"))return false;
    }
    constexpr std::array<std::uint64_t,3> platforms{0x01100001FFC00001ULL,0x01100001FFC00002ULL,0x01100001FFC00003ULL};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    std::array<std::uint64_t,3> lobbies{};
    const auto friends=+[](std::uint64_t first,std::uint64_t second) noexcept {
        return (first==0x01100001FFC00001ULL && second==0x01100001FFC00002ULL)
            || (first==0x01100001FFC00002ULL && second==0x01100001FFC00001ULL);
    };
    for(const auto i:std::array<std::size_t,3>{0,2,1}) {
        if(!realtime_store::begin_game_session(platforms[i],accounts[i].primarySoid,1,now)
            || !realtime_store::mark_gameplay_ready(platforms[i],accounts[i].primarySoid,1,now))return false;
        realtime::Request request{};realtime::Response response{};
        request.operation=i==1?realtime::Operation::joinLobby:realtime::Operation::createLobby;
        request.valueA=2;request.valueB=3;if(i==1)request.lobbyId=lobbies[0];
        if(!check(realtime_store::execute(platforms[i],accounts[i].primarySoid,now,request,friends,response)
            && response.result==1,"two-person fireteam and independent visitor"))return false;
        lobbies[i]=response.lobby.id;
    }
    for(std::size_t from=0;from<3;++from)for(std::size_t to=0;to<3;++to) {
        if(from==to)continue;
        realtime::Request request{};realtime::Response response{};
        request.operation=realtime::Operation::sendRendezvous;request.lobbyId=lobbies[from];
        request.targetPlatformId=platforms[to];request.payloadSize=8;
        if(!check(realtime_store::execute(platforms[from],accounts[from].primarySoid,now,request,nullptr,response)
            && response.result==1 && response.lobby.id==lobbies[from]
            && response.lobby.memberCount==(from==2?1:2),"six signaling directions without fireteam merge"))return false;
        std::array<realtime::Request,3> packets{request,request,request};
        for(std::size_t i=0;i<packets.size();++i)packets[i].payload[4]=static_cast<std::byte>(i+1);
        realtime::Request batch{};
        const bool encodedBatch=middleware::social::rendezvous_batch::encode(packets,batch);
        const bool executedBatch=encodedBatch && realtime_store::execute(platforms[from],accounts[from].primarySoid,now,batch,nullptr,response);
        if(!encodedBatch || !executedBatch || response.result!=1 || response.lobby.id!=lobbies[from])
            std::printf("batch detail from=%zu to=%zu encoded=%u executed=%u result=%d payload=%zu operation=%u lobby_match=%u\n",
                from,to,encodedBatch?1U:0U,executedBatch?1U:0U,response.result,batch.payloadSize,
                static_cast<unsigned>(batch.operation),response.lobby.id==lobbies[from]?1U:0U);
        if(!check(encodedBatch && executedBatch
            && response.result==1 && response.lobby.id==lobbies[from],
            "ordered batch uses the same six authorized signaling routes"))return false;
        realtime::Request poll{};bool found=false;
        for(unsigned pass=0;pass<8 && !found;++pass) {
            if(!realtime_store::execute(platforms[to],accounts[to].primarySoid,now,poll,nullptr,response))return false;
            for(std::size_t e=0;e<response.eventCount;++e) {
                const auto& event=response.events[e];
                if(event.kind!=realtime::EventKind::rendezvousBatch || event.sourcePlatformId!=platforms[from])continue;
                middleware::social::rendezvous_batch::View view{};
                if(!check(event.lobbyId==lobbies[from] && event.memberPlatformId==platforms[to]
                    && middleware::social::rendezvous_batch::decode(std::span(event.payload).first(event.payloadSize),view)
                    && view.count==packets.size(),"recipient receives routed complete envelope"))return false;
                for(std::size_t i=0;i<view.count;++i)
                    if(!check(std::equal(view.packets[i].begin(),view.packets[i].end(),packets[i].payload.begin(),
                        packets[i].payload.begin()+packets[i].payloadSize),"native connection ids and packet order preserved"))return false;
                found=true;
            }
            poll.cursor=response.cursor;
        }
        if(!check(found,"batch is delivered rather than only accepted"))return false;
        batch.payload[1]=std::byte{0};
        if(!check(realtime_store::execute(platforms[from],accounts[from].primarySoid,now,batch,nullptr,response)
            && response.result==8,"malformed batch rejected before delivery"))return false;
    }
    if(!check(lobbies[0]==lobbies[1] && lobbies[0]!=lobbies[2],"fireteam identity preserved"))return false;
    for(std::size_t i=0;i<3;++i)a::shared::disconnect(contexts[i],host);
    {
        realtime::Request packet{},batch{};realtime::Response response{};
        packet.operation=realtime::Operation::sendRendezvous;packet.lobbyId=lobbies[0];
        packet.targetPlatformId=platforms[2];packet.payloadSize=8;
        if(!check(middleware::social::rendezvous_batch::encode(std::span(&packet,1),batch)
            && realtime_store::execute(platforms[0],accounts[0].primarySoid,now,batch,nullptr,response)
            && response.result==15,"departed independent peer has no batch permission"))return false;
        batch.lobbyId=lobbies[2];
        if(!check(realtime_store::execute(platforms[0],accounts[0].primarySoid,now,batch,nullptr,response)
            && response.result==15,"batch cannot impersonate another sender lobby"))return false;
    }
    for(const auto i:std::array<std::size_t,3>{1,0,2})
        if(!realtime_store::remove_lobby_member(lobbies[i],platforms[i],platforms[i]))return false;
    std::printf("PASS: mixed social party + visitor, native PRIVATE primary, six signaling directions, visitor_first=%u\n",visitorFirst?1U:0U);
    return true;
}
inline bool social_primary_admission_self_test(std::uint64_t baseline) noexcept {
    return social_primary_case(baseline,"city_tower_social_d2",20,0xE8ABA41B)
        && social_primary_case(baseline,"campaign_social_space_d2",22,0x4E0FA012)
        && social_primary_case(baseline,"tangled_shore_social_space",44,0x34B43AB2)
        && social_primary_case(baseline,"campaign_social_space_d2",23,0x5D813A4C)
        && social_primary_case(baseline,"campaign_social_space_d2",24,0x13B3BA49)
        && social_primary_case(baseline,"campaign_social_space_d2",25,0xD873EF56)
        && social_primary_case(baseline,"campaign_social_space_d2",26,0x4CEF34A6)
        && social_primary_case(baseline,"city_tower_social_d2",84,0x59905E37)
        && social_party_case(baseline,false) && social_party_case(baseline,true);
}
}
