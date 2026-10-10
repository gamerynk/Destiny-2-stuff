#pragma once

#include <array>
#include <cstdio>
#include <cstring>

#include "state/activity/destination/activity_destination_snapshot.h"
#include "state/activity/entity_slots/runtime.h"
#include "state/activity/membership/activity_membership_query.h"
#include "state/activity/runtime.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"

namespace restoration::tests {

inline bool activity_account_context_self_test(std::uint64_t ownerSession) noexcept {
    using namespace dawn;
    namespace activity = state::activity;
    namespace slots = activity::entity_slots;
    namespace membership = activity::membership;
    const auto check = [](bool passed, const char* step) noexcept {
        if (!passed) std::printf("FAIL: activity account context: %s\n", step);
        return passed;
    };
    activity::destination::DestinationSelection selection{};
    if (!check(activity::destination::snapshot(ownerSession, selection), "owner fixture"))
        return false;

    static std::array<std::byte, sizeof(state::State)> ownerBefore{};
    std::memcpy(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size());
    static std::array<state::AccountState, 2> accounts{};
    static std::array<activity::ActivityState, 2> worlds{};
    const auto owner = state::account_snapshot();
    for (std::size_t index = 0; index < accounts.size(); ++index) {
        accounts[index] = owner;
        accounts[index].primarySoid ^= (static_cast<std::uint64_t>(index) + 1) << 32;
        worlds[index] = {};
    }
    const std::array<state::AccountRuntimeContext, 2> contexts{{
        {&accounts[0], &worlds[0]}, {&accounts[1], &worlds[1]}}};
    std::array<std::uint64_t, 2> sessions{};
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime scope(contexts[index]);
        activity::PendingAllocation allocation{};
        if (!check(activity::prepare_session(selection, sessions[index], allocation)
                   && activity::commit(allocation), "scoped allocation")) return false;
        std::uint64_t startupSession{};
        const auto key=0x1020304050607000ULL+index;
        const std::array<membership::Identity,2> startupPeers{{
            {key,-1,-1,key+10,accounts[index].primarySoid,accounts[index].primarySoid+1,key+20},
            {key+100,-1,-1,key+110,accounts[1-index].primarySoid,accounts[1-index].primarySoid+1,key+120}}};
        if (!check(activity::prepare_session(selection,startupSession,allocation)
            && activity::prepare_startup_reservations(allocation,startupPeers)
            && allocation.startupReservations[0].memberKey==key+100,
            "startup reserves only remote peer")) return false;
        auto tampered=allocation;
        tampered.startupReservations[0].memberKey^=1;
        if (!check(!activity::commit(tampered),"startup tamper rejection")) return false;
        {
            const state::ScopedAccountRuntime foreign(contexts[1-index]);
            auto wrong=allocation;
            if (!check(!activity::commit(wrong),"startup foreign commit rejection")) return false;
        }
        if (!check(activity::commit(allocation) && !activity::commit(allocation),
            "startup atomic allocation and replay")) return false;
        slots::PendingMutation join{};
        membership::PendingMutation initial{};
        if (!check(slots::prepare_join(startupSession,key,64,8,join) && slots::commit(join),
            "startup host join")) return false;
        if (!check(membership::prepare_refresh(startupSession,0,-1,initial)
            && !initial.hasSnapshot && membership::commit(initial),
            "startup pre-identity refresh remains unpublished")) return false;
        if (!check(membership::prepare_identity(startupSession,startupPeers[0],initial)
            && initial.hasSnapshot
            && initial.snapshot.reservations[0].memberKey==key+100
            && initial.snapshot.reservations[0].accountSoid==accounts[1-index].primarySoid
            && membership::commit(initial),"startup reservation survives host identity")) return false;
        if (!check(membership::prepare_refresh(startupSession,0,-1,initial)
            && initial.hasSnapshot
            && initial.snapshot.reservations[0].memberKey==key+100
            && initial.snapshot.reservations[0].accountSoid==accounts[1-index].primarySoid
            && membership::commit(initial),"startup reservation survives published refresh")) return false;
    }

    constexpr std::array<std::size_t, 4> order{0, 1, 1, 0};
    for (const auto index : order) {
        const auto other = 1 - index;
        static std::array<std::byte, sizeof(activity::ActivityState)> otherBefore{};
        std::memcpy(otherBefore.data(), &worlds[other], otherBefore.size());
        {
            const state::ScopedAccountRuntime scope(contexts[index]);
            const auto session = sessions[index];
            const auto key = 0x123456789ABCDE00ULL + index;
            slots::PendingMutation lease{};
            if (!check(slots::prepare_join(session, key, 64, 8, lease),
                       "remote join prepare")) return false;
            if (!check(slots::commit(lease) && !slots::commit(lease)
                       && activity::is_joined(session)
                       && membership::member_key(session) == key,
                       "remote join commit and replay guard")) return false;
            if (!check(!slots::prepare_join(ownerSession, key, 64, 8, lease)
                       && !slots::prepare_join(sessions[other], key, 64, 8, lease),
                       "foreign session join denied")) return false;

            if (!check(slots::prepare_grant(session, 4, lease), "remote slot grant prepare"))
                return false;
            const auto returned = lease.mask;
            if (!check(slots::commit(lease)
                       && slots::prepare_release(session, returned, lease)
                       && lease.mask == returned && slots::commit(lease),
                       "remote slot grant/release commit")) return false;

            membership::PendingMutation member{};
            if (!check(membership::prepare_refresh(session, 0, -1, member)
                       && membership::commit(member), "remote pre-identity refresh")) return false;
            membership::Identity identity{};
            identity.memberKey = key;
            identity.joinIdentity = key + 10;
            identity.accountSoid = accounts[index].primarySoid;
            identity.opaqueSoid = identity.accountSoid + 1;
            if (!check(membership::prepare_identity(session, identity, member)
                       && membership::commit(member) && !membership::commit(member)
                       && membership::account_soid(session) == identity.accountSoid
                       && membership::join_identity(session) == identity.joinIdentity,
                       "remote identity commit and replay guard")) return false;
            auto wrongIdentity = identity;
            membership::Identity reserved{key+100,-1,-1,key+101,
                accounts[other].primarySoid,accounts[other].primarySoid+1,key+102};
            const std::array<membership::Identity,1> peers{reserved};
            if (!check(!membership::prepare_reservations(session,1,peers,0,member)
                && !membership::prepare_reservations(sessions[other],0,peers,0,member)
                && membership::prepare_reservations(session,0,peers,0,member)
                && member.changesState && member.snapshot.reservations[0].memberKey==reserved.memberKey
                && membership::commit(member) && !membership::commit(member),
                "reservation epoch, account scope, commit and replay")) return false;
            if (!check(membership::prepare_reservations(session,0,peers,0,member)
                && !member.changesState && membership::commit(member)
                && membership::prepare_refresh(session,0,-1,member)
                && member.snapshot.reservations[0].memberKey==reserved.memberKey,
                "reservation duplicate and refresh persistence")) return false;
            if (!check(membership::prepare_reservations(session,0,{},reserved.memberKey,member),
                "reservation release prepare")) return false;
            member.snapshot.reservations[0]=reserved;
            if (!check(!membership::commit(member),"tampered reservation plan rejected")) return false;
            if (!check(membership::prepare_reservations(session,0,{},reserved.memberKey,member)
                && membership::commit(member)
                && membership::prepare_refresh(session,0,-1,member)
                && member.snapshot.reservations[0].memberKey==0
                && member.snapshot.identity.memberKey==key,
                "reservation release preserves local identity")) return false;
            wrongIdentity.memberKey ^= 1;
            if (!check(!membership::prepare_identity(session, wrongIdentity, member),
                       "wrong member key denied")) return false;

            membership::AuthoritativeUpdate update{};
            update.hasRegion = true;
            update.region.index = 120 + static_cast<std::int32_t>(index);
            update.region.hash = 0x12340000U + static_cast<std::uint32_t>(index);
            update.hasTransitionToken = true;
            update.transitionToken = 2;
            if (!check(membership::prepare_authoritative(session, update, member)
                       && membership::commit(member)
                       && membership::reported_region(session) == update.region.index,
                       "remote authoritative region and token")) return false;
            if (!check(membership::prepare_refresh(session, 0, -1, member)
                       && member.hasSnapshot && member.snapshot.identity.accountSoid == identity.accountSoid
                       && member.snapshot.transitionToken == 2,
                       "remote refresh snapshot")) return false;
            auto revision = member.snapshot.revision;
            if (!check(membership::commit(member)
                       && membership::prepare_acknowledgement(session, revision, member)
                       && membership::commit(member) && membership::acknowledged(session)
                       && membership::republish(session) && !membership::acknowledged(session),
                       "remote acknowledgement and republish")) return false;
            if (!check(membership::prepare_refresh(session, 0, -1, member)
                       && member.snapshot.revision == revision + 1 && membership::commit(member),
                       "republish revision")) return false;

            if (!check(membership::prepare_acknowledgement(session, revision, member)
                       && membership::republish(session) && !membership::commit(member),
                       "stale membership transaction denied")) return false;
            if (!check(slots::prepare_grant(session, 1, lease)
                       && membership::republish(session) && !slots::commit(lease),
                       "stale slot transaction denied")) return false;
            if (!check(!membership::prepare_identity(ownerSession, identity, member)
                       && !membership::prepare_refresh(sessions[other], 0, -1, member)
                       && !membership::prepare_authoritative(sessions[other], update, member)
                       && !membership::prepare_acknowledgement(sessions[other], 1, member)
                       && !membership::republish(ownerSession),
                       "foreign membership access denied")) return false;

            if (!check(slots::prepare_grant(session, 1, lease)
                       && membership::prepare_refresh(session, 0, -1, member),
                       "cross-context plans prepared")) return false;
            {
                const state::ScopedAccountRuntime nested(contexts[other]);
                if (!check(!slots::commit(lease) && !membership::commit(member),
                           "cross-context commits denied")) return false;
            }
            if (!check(state::account_snapshot().primarySoid == accounts[index].primarySoid,
                       "nested scope restored")) return false;
        }
        if (!check(std::memcmp(otherBefore.data(), &worlds[other], otherBefore.size()) == 0,
                   "other account unchanged")) return false;
        if (!check(std::memcmp(ownerBefore.data(), &state::runtime::storage::g_state,
                               ownerBefore.size()) == 0
                   && state::account_snapshot().primarySoid == owner.primarySoid,
                   "process owner unchanged and scope restored")) return false;
    }
    return true;
}

} // namespace restoration::tests
