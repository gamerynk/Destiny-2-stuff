#pragma once
#include "state/activity/shared/runtime.h"
#include "state/activity/runtime.h"
#include "state/activity/destination/activity_destination_snapshot.h"
#include "state/activity/entity_slots/transactions/internal.h"
#include "state/runtime/storage/internal.h"

namespace restoration::tests {
inline bool late_private_join_self_test(std::uint64_t fixtureSession) noexcept {
    using namespace dawn;
    namespace activity=state::activity;
    namespace shared=activity::shared;
    namespace slots=activity::entity_slots;
    namespace member=activity::membership;
    const auto check=[](bool ok,const char* step) noexcept {
        if(!ok)std::printf("FAIL: late private join: %s\n",step);return ok;
    };
    activity::destination::DestinationSelection destination{};
    if(!check(activity::destination::snapshot(fixtureSession,destination),"fixture"))return false;
    static std::array<state::AccountState,7> accounts{};
    static std::array<activity::ActivityState,7> worlds{};
    std::array<state::AccountRuntimeContext,7> contexts{};
    std::array<member::Identity,7> identities{};
    for(std::size_t i=0;i<accounts.size();++i) {
        accounts[i]=state::account_snapshot();accounts[i].primarySoid=0x8AA0300000000100ULL+(i<<32);
        worlds[i]={};contexts[i]={&accounts[i],&worlds[i]};
        identities[i]={0x1020304050607001ULL+i,-1,-1,0xAB01020304050601ULL+i,
            accounts[i].primarySoid,accounts[i].primarySoid+1,0x1201+i};
    }
    std::uint64_t session{};
    slots::PendingMutation plan{};
    constexpr auto grant=slots::kSlotCount-8;
    {
        const state::ScopedAccountRuntime owner(contexts[0]);
        activity::PendingAllocation allocation{};member::PendingMutation identity{};
        if(!check(activity::prepare_session(destination,session,allocation)
            && activity::prepare_startup_reservations(allocation,std::span(identities).first(1))
            && activity::commit(allocation) && slots::prepare_join(session,identities[0].memberKey,grant,8,plan)
            && slots::commit(plan) && member::prepare_identity(session,identities[0],identity)
            && member::commit(identity),"real solo startup, lease and identity"))return false;
    }
    state::AccountRuntimeContext owner{};
    if(!check(shared::owner_context(session,owner) && owner.account==contexts[0].account,
        "live solo private session is discoverable for later authorized join"))return false;
    activity::SessionRecord before{},one{},two{},after{};
    if(!check(activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,session,before)
        && before.sharedLeaseParts==shared::kPrivateLeaseParts
        && before.membership.reservations[0].memberKey==0,"capacity exists without invented reservations"))return false;
    slots::PendingMutation first{},second{};
    const auto prepare=[&](std::size_t i,slots::PendingMutation& out) noexcept {
        return shared::prepare_join(owner,session,identities[i].memberKey,identities[i].opaqueSoid,
            grant,8,out,nullptr,&identities[i]);
    };
    {
        const state::ScopedAccountRuntime guest(contexts[1]);
        if(!check(!shared::prepare_join(owner,session,identities[1].memberKey,identities[1].opaqueSoid,
            grant,8,plan),"unreserved ordinary join still denied"))return false;
        auto wrong=identities[1];wrong.accountSoid=accounts[2].primarySoid;
        if(!check(!shared::prepare_join(owner,session,identities[1].memberKey,identities[1].opaqueSoid,
            grant,8,plan,nullptr,&wrong),"foreign authenticated identity denied"))return false;
        if(!check(prepare(1,first),"late peer stages without mutating owner"))return false;
        auto tampered=first;tampered.privateAdmission.joinIdentity^=1;
        if(!check(!slots::commit(tampered),"changed identity plan denied"))return false;
        auto failing=contexts[1];
        failing.activityPersistence.save=[](void*,const activity::ActivityState&) noexcept {return false;};
        {
            const state::ScopedAccountRuntime sink(failing);auto failed=first;
            if(!check(!slots::commit(failed) && !activity::contains(session),"failed persistence cannot admit peer"))return false;
        }
    }
    {
        const state::ScopedAccountRuntime guest(contexts[2]);
        if(!check(prepare(2,second),"concurrent second peer plan"))return false;
    }
    {
        const state::ScopedAccountRuntime guest(contexts[1]);
        member::PendingMutation identity{};
        if(!check(slots::commit(first) && !slots::commit(first)
            && member::prepare_identity(session,identities[1],identity) && member::commit(identity),
            "late peer commits exact lease and native identity; replay refused"))return false;
    }
    {
        const state::ScopedAccountRuntime guest(contexts[2]);
        if(!check(!slots::commit(second) && prepare(2,second) && slots::commit(second),
            "concurrent lease collision refused, refreshed slot succeeds"))return false;
        member::PendingMutation identity{};
        if(!check(member::prepare_identity(session,identities[2],identity) && member::commit(identity),
            "second peer identity"))return false;
    }
    if(!check(activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,session,after)
        && activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,session,one)
        && activity::snapshot_for_runtime(contexts[2],accounts[2].primarySoid,session,two)
        && before.heldEntitySlots==after.heldEntitySlots && before.serverEntitySlots==after.serverEntitySlots
        && one.sharedMemberSlot==1 && two.sharedMemberSlot==2,"owner lease unchanged; stable independent slots"))return false;
    for(std::size_t i=0;i<before.heldEntitySlots.size();++i)
        if(!check((before.heldEntitySlots[i]&one.heldEntitySlots[i])==std::byte{}
            && (before.heldEntitySlots[i]&two.heldEntitySlots[i])==std::byte{}
            && (one.heldEntitySlots[i]&two.heldEntitySlots[i])==std::byte{},"leases never overlap"))return false;
    {
        const state::ScopedAccountRuntime host(contexts[0]);
        member::PendingMutation refresh{};shared::Projection projected{};
        if(!check(member::prepare_refresh(session,0,-1,refresh)
            && shared::project(session,refresh.snapshot,projected) && projected.joinedMemberMask==7
            && projected.members[1].memberKey==identities[1].memberKey
            && projected.members[2].memberKey==identities[2].memberKey,"owner sees real late identities"))return false;
    }
    {
        const state::ScopedAccountRuntime host(contexts[0]);member::PendingMutation reservation{};
        const auto third=shared::as_reservation(identities[3]);
        if(!check(member::prepare_reservations(session,member::kStableEpoch,std::span(&third,1),0,reservation)
            && reservation.snapshot.reservations[0].memberKey==identities[1].memberKey
            && reservation.snapshot.reservations[1].memberKey==identities[2].memberKey
            && reservation.snapshot.reservations[2].memberKey==identities[3].memberKey
            && member::commit(reservation),"late native reservations retain occupied canonical slots"))return false;
    }
    for(std::size_t i=3;i<6;++i) {
        const state::ScopedAccountRuntime guest(contexts[i]);member::PendingMutation identity{};
        if(!check(prepare(i,plan) && slots::commit(plan)
            && member::prepare_identity(session,identities[i],identity) && member::commit(identity),
            "remaining late peers fit original capacity"))return false;
    }
    {
        const state::ScopedAccountRuntime guest(contexts[6]);
        if(!check(!prepare(6,plan),"seventh player denied without changing leases"))return false;
    }
    auto all=before.heldEntitySlots;
    for(std::size_t i=1;i<6;++i) {
        activity::SessionRecord row{};
        if(!check(activity::snapshot_for_runtime(contexts[i],accounts[i].primarySoid,session,row),"capacity snapshot"))return false;
        if(!check(slots::transactions::empty(slots::transactions::intersect(all,row.heldEntitySlots)),"all six lanes disjoint"))return false;
        for(std::size_t n=0;n<all.size();++n)all[n]|=row.heldEntitySlots[n];
    }
    if(!check(slots::transactions::count(all)==grant,"all six lanes cover exactly the client capacity"))return false;
    for(std::size_t i=3;i<6;++i)shared::disconnect(contexts[i],session);
    shared::disconnect(contexts[1],session);shared::disconnect(contexts[2],session);
    shared::disconnect(contexts[0],session);
    if(!check(!shared::owner_context(session,owner),"last departure retires canonical owner"))return false;
    std::puts("PASS: late private join: solo startup capacity, authorized identity, stable owner lease, concurrent slots, projection and cleanup");
    return true;
}
}
