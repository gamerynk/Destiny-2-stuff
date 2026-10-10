#pragma once

#include <array>
#include <cstdio>
#include <cstring>

#include "server/gameplay/group/group_host.h"
#include "server/gameplay/group/group_host_sessions.h"
#include "state/activity/destination/activity_destination_snapshot.h"
#include "state/activity/entity_slots/runtime.h"
#include "state/activity/membership/activity_membership_query.h"
#include "state/activity/runtime.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"

namespace restoration::tests {

struct PublicRegionTestSink {
    std::size_t calls{};
    bool reject{};
    static bool save(void* opaque, const dawn::state::activity::ActivityState&) noexcept {
        auto& sink = *static_cast<PublicRegionTestSink*>(opaque);
        ++sink.calls;
        return !sink.reject;
    }
};

inline bool public_region_account_context_self_test(std::uint64_t ownerSession) noexcept {
    using namespace dawn;
    namespace group = server::gameplay::group;
    namespace activity = state::activity;
    const auto check = [](bool passed, const char* step) noexcept {
        if (!passed) std::printf("FAIL: public region account context: %s\n", step);
        return passed;
    };
    activity::destination::DestinationSelection selection{};
    if (!activity::destination::snapshot(ownerSession, selection)) return false;
    constexpr char privateName[]="mission_context_fixture";
    selection.packageName={};
    selection.packageNameLength=sizeof(privateName)-1;
    std::copy_n(privateName,sizeof(privateName)-1,selection.packageName.begin());
    static std::array<state::AccountState, 2> accounts{};
    static std::array<activity::ActivityState, 2> worlds{};
    static std::array<PublicRegionTestSink, 2> sinks{};
    sinks = {};
    static std::array<std::byte, sizeof(state::State)> ownerBefore{};
    const auto owner = state::account_snapshot();
    std::memcpy(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size());
    for (std::size_t index = 0; index < accounts.size(); ++index) {
        accounts[index] = owner;
        accounts[index].primarySoid ^= std::uint64_t{4} << 32;
        accounts[index].primarySoid += index * 0x100;
        worlds[index] = {};
    }
    const std::array<state::AccountRuntimeContext, 2> contexts{{
        {&accounts[0], &worlds[0], {}, {&sinks[0], PublicRegionTestSink::save}},
        {&accounts[1], &worlds[1], {}, {&sinks[1], PublicRegionTestSink::save}}}};
    constexpr std::array<std::uint64_t, 2> groups{0x7100000000000001ULL, 0x7100000000000002ULL};
    std::array<std::uint64_t, 2> sources{}, hosts{};
    struct Cleanup {
        ~Cleanup() { group::reset_host_sessions(); }
    } cleanup;
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime scope(contexts[index]);
        auto destination = selection;
        destination.activityIndex = static_cast<std::int16_t>(8 + index);
        activity::PendingAllocation allocation{};
        activity::entity_slots::PendingMutation join{};
        activity::membership::PendingMutation membership{};
        activity::membership::Identity identity{};
        identity.memberKey = 0x1122334400000000ULL + index;
        identity.accountSoid = accounts[index].primarySoid;
        identity.joinIdentity = identity.memberKey + 20;
        if (!check(activity::prepare_session(destination, sources[index], allocation)
                   && activity::commit(allocation)
                   && activity::entity_slots::prepare_join(sources[index], identity.memberKey, 64, 8, join)
                   && activity::entity_slots::commit(join)
                   && activity::membership::prepare_identity(sources[index], identity, membership)
                   && activity::membership::commit(membership), "remote private fixture")) return false;
        bool claimed = false;
        if (!check(group::activity_host_session(groups[index], 408 + static_cast<std::int32_t>(index),
                                               sources[index], claimed) == 0 && claimed,
                   "remote advertisement claim")) return false;
    }
    group::allocate_claimed_host_sessions();
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        hosts[index] = group::held_host_session(groups[index]);
        const state::ScopedAccountRuntime scope(contexts[index]);
        activity::destination::DestinationSelection destination{};
        if (!check(hosts[index] != 0 && activity::contains(hosts[index])
                   && activity::destination::snapshot(hosts[index], destination)
                   && destination.activityIndex == static_cast<std::int16_t>(8 + index),
                   "background host allocated in source account with source destination")) return false;
        if (!check(group::source_activity_session_for_host(hosts[index]) == sources[index],
                   "reverse lookup")) return false;
        if (!check(group::region_for_host_session(hosts[index]) == 408 + static_cast<std::int32_t>(index),
                   "colliding session IDs reverse-resolve only within authenticated account")) return false;
        if (!check(sinks[index].calls == 4, "background allocation retains durable sink")) return false;
    }
    if (!check(hosts[0] == hosts[1] && sources[0] == sources[1],
               "fixture deliberately collides per-account session IDs")) return false;
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime scope(contexts[index]);
        const auto revision = worlds[index].stateRevision;
        const auto saves = sinks[index].calls;
        for (unsigned request = 0; request < 64; ++request) {
            bool claimed = false;
            if (!check(group::activity_host_session(groups[index],
                           408 + static_cast<std::int32_t>(index), hosts[index], claimed) == hosts[index]
                       && claimed && group::source_activity_session(groups[index]) == sources[index]
                       && group::source_activity_session_for_host(hosts[index]) == sources[index],
                       "PUBLIC self-advertisement remains ready and keeps PRIVATE source")) return false;
            if (!check(group::activity_host_session(groups[index],
                           408 + static_cast<std::int32_t>(index), sources[index], claimed) == hosts[index]
                       && claimed && worlds[index].stateRevision == revision && sinks[index].calls == saves,
                       "alternating PRIVATE/PUBLIC requests never allocate, retire or mutate State")) return false;
        }
    }
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime wrongScope(contexts[1 - index]);
        bool claimed = false;
        if (!check(group::activity_host_session(groups[index], 408 + static_cast<std::int32_t>(index),
                                               hosts[1 - index], claimed) == 0 && claimed
                   && group::held_host_session(groups[index]) == hosts[index]
                   && group::source_activity_session(groups[index]) == sources[index],
                   "equal numeric PUBLIC IDs cannot borrow another account's host")) return false;
        activity::SessionRecord source{};
        if (!check(group::source_activity_snapshot(groups[index], source)
                   && source.membership.hasIdentity && source.joined
                   && source.ownerAccountSoid == accounts[index].primarySoid
                   && source.membership.identity.accountSoid == accounts[index].primarySoid
                   && source.memberKey == 0x1122334400000000ULL + index
                   && source.destination.activityIndex == static_cast<std::int16_t>(8 + index),
                   "gameplay worker reads exact source identity/machine/destination despite wrong TLS")) return false;
        if (!check(!activity::snapshot_for_runtime({}, accounts[index].primarySoid, sources[index], source)
                   && !source.occupied
                   && !activity::snapshot_for_runtime(contexts[index], accounts[1 - index].primarySoid,
                                                      sources[index], source)
                   && !source.occupied,
                   "missing context and mismatched owner fail closed")) return false;
    }
    constexpr std::uint64_t alias = 0x7100000000000010ULL;
    activity::SessionRecord aliasedSource{};
    if (!check(group::alias_host_session(groups[0], alias) == hosts[0]
               && group::source_activity_snapshot(alias, aliasedSource)
               && aliasedSource.ownerAccountSoid == accounts[0].primarySoid,
               "migration alias retains source account")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        bool claimed = false;
        if (!check(group::activity_host_session(alias, 408, hosts[0], claimed) == hosts[0] && claimed
                   && group::source_activity_session(alias) == sources[0]
                   && group::activity_host_session(alias, group::kUnknownRegion) == hosts[0]
                   && group::region_for_host_session(hosts[0]) == 408,
                   "PUBLIC self-advertisement and bodyless lookup preserve migration alias")) return false;
    }
    if (!check(group::source_activity_session_for_host(hosts[0]) == 0
               && group::region_for_host_session(hosts[0]) == group::kUnknownRegion,
               "process owner cannot reverse-resolve remote host")) return false;
    if (!check(std::memcmp(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size()) == 0,
               "background allocation leaves process owner unchanged")) return false;
    group::reset_host_sessions();
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime scope(contexts[index]);
        if (!check(!activity::contains(hosts[index]) && activity::is_joined(sources[index]),
                   "background cleanup releases only derived host in correct account")) return false;
    }
    if (!check(std::memcmp(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size()) == 0,
               "cleanup leaves process owner unchanged")) return false;

    static activity::ActivityState rejectedBefore{};
    rejectedBefore = worlds[0];
    constexpr std::uint64_t staleGroup = 0x7200000000000000ULL;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        bool claimed = false;
        (void)group::activity_host_session(staleGroup, 411, 0x7FFFFFFFFFFFFF00ULL, claimed);
    }
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const state::ScopedAccountRuntime scope(contexts[index]);
        bool claimed = false;
        (void)group::activity_host_session(groups[index], 408 + static_cast<std::int32_t>(index),
                                           sources[index], claimed);
    }
    sinks[0].reject = true;
    group::allocate_claimed_host_sessions();
    if (!check(group::held_host_session(staleGroup) == 0 && group::held_host_session(groups[0]) == 0
               && group::held_host_session(groups[1]) != 0
               && std::memcmp(&worlds[0], &rejectedBefore, sizeof rejectedBefore) == 0,
               "failed save and stale source remain unpublished without starving other account")) return false;
    sinks[0].reject = false;
    group::allocate_claimed_host_sessions();
    if (!check(group::held_host_session(groups[0]) != 0, "durable allocation retries successfully")) return false;
    group::reset_host_sessions();

    std::uint64_t evictedHost = 0;
    for (std::size_t index = 0; index < 9; ++index) {
        const auto accountIndex = index % contexts.size();
        const auto groupId = 0x7300000000000000ULL + index;
        {
            const state::ScopedAccountRuntime scope(contexts[accountIndex]);
            bool claimed = false;
            (void)group::activity_host_session(groupId, 420 + static_cast<std::int32_t>(index),
                                               sources[accountIndex], claimed);
            if (!check(claimed, "eviction fixture claims region")) return false;
        }
        {
            const state::ScopedAccountRuntime wrongScope(contexts[1 - accountIndex]);
            group::allocate_claimed_host_sessions();
            if (!check(state::account_snapshot().primarySoid == accounts[1 - accountIndex].primarySoid,
                       "worker restores previous account scope")) return false;
        }
        if (index == 0) evictedHost = group::held_host_session(groupId);
        if (!check(group::held_host_session(groupId) != 0, "eviction fixture allocates region")) return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        if (!check(!activity::contains(evictedHost) && activity::is_joined(sources[0]),
                   "eviction releases derived session in its own account")) return false;
    }
    group::reset_host_sessions();
    for (std::size_t index = 0; index < worlds.size(); ++index) {
        for (const auto& record : worlds[index].sessions) {
            if (!check(!record.occupied || record.sessionId == sources[index],
                       "reset leaves no stranded derived hosts")) return false;
        }
    }
    if (!check(std::memcmp(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size()) == 0,
               "persistence/eviction/cleanup leave process owner byte-exact unchanged")) return false;

    constexpr std::uint64_t returnGroup=0x7400000000000001ULL;
    constexpr std::uint64_t returnAlias=0x7400000000000002ULL;
    std::uint64_t oldHost{};
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        bool claimed=false;
        if (!check(group::activity_host_session(returnGroup,40,sources[0],claimed)==0 && claimed,
                   "return fixture claims region")) return false;
    }
    group::allocate_claimed_host_sessions();
    oldHost=group::held_host_session(returnGroup);
    if (!check(oldHost!=0 && group::alias_host_session(returnGroup,returnAlias)==oldHost,
               "return fixture host and migration alias")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        bool claimed=false;
        if (!check(group::activity_host_session(returnGroup,40,sources[0],claimed)==oldHost
                   && claimed,"same PRIVATE source retains exact PUBLIC host for fast travel")) return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        bool claimed=false;
        if (!check(group::activity_host_session(returnGroup,40,sources[1],claimed)==0 && claimed
                   && group::held_host_session(returnGroup)==oldHost,
                   "another source cannot replace a live PRIVATE instance")) return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        if (!check(activity::release_session(sources[0]),"retire original PRIVATE source")) return false;
    }

    std::size_t rootSlot=activity::kInvalidSessionSlot,guestSlot=activity::kInvalidSessionSlot;
    for (std::size_t index=0;index<worlds[0].sessions.size();++index)
        if (worlds[0].sessions[index].occupied && worlds[0].sessions[index].sessionId==oldHost) rootSlot=index;
    for (std::size_t index=0;index<worlds[1].sessions.size();++index)
        if (!worlds[1].sessions[index].occupied) {guestSlot=index;break;}
    if (!check(rootSlot!=activity::kInvalidSessionSlot && guestSlot!=activity::kInvalidSessionSlot,
               "surviving PUBLIC guest fixture slots")) return false;
    auto& root=worlds[0].sessions[rootSlot];
    root.sharedOwnerAccountSoid=root.ownerAccountSoid;
    root.sharedOwnerCreatedRevision=root.createdRevision;
    root.sharedLeaseParts=2;
    root.sharedOwnerDetached=true;
    root.membership.hasIdentity=true;
    root.membership.identity.memberKey=0x1122334400000000ULL;
    auto& guest=worlds[1].sessions[guestSlot];
    guest={};guest.occupied=true;guest.joined=true;guest.sessionId=oldHost;
    guest.ownerAccountSoid=accounts[1].primarySoid;
    guest.sharedOwnerAccountSoid=accounts[0].primarySoid;
    guest.sharedOwnerCreatedRevision=root.createdRevision;
    guest.sharedLeaseParts=2;guest.sharedMemberSlot=1;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        bool claimed=false;
        state::AccountRuntimeContext canonical{};
        const auto ownerRevision=worlds[0].stateRevision;
        const auto guestRevision=worlds[1].stateRevision;
        if (!check(activity::shared::canonical_context(oldHost,canonical)
                   && canonical.account==contexts[0].account && canonical.activity==contexts[0].activity
                   && group::activity_host_session(returnAlias,40,oldHost,claimed)==oldHost && claimed
                   && group::held_host_session(returnGroup)==oldHost
                   && group::source_activity_session(returnAlias)==sources[0]
                   && worlds[0].stateRevision==ownerRevision && worlds[1].stateRevision==guestRevision,
                   "admitted PUBLIC guest uses canonical host without rebinding its retired PRIVATE source")) return false;
        if (!check(activity::shared::has_joined_participants(contexts[0],accounts[0].primarySoid,oldHost)
                   && group::activity_host_session(returnGroup,40,sources[1],claimed)==0 && claimed
                   && group::held_host_session(returnGroup)==oldHost
                   && group::source_activity_session(returnGroup)==sources[0],
                   "detached owner never retires a PUBLIC world containing an identity-pending guest")) return false;
    }
    guest={};
    if (!check(!activity::shared::has_joined_participants(contexts[0],accounts[0].primarySoid,oldHost)
               && activity::shared::has_joined_participants({},accounts[0].primarySoid,oldHost)
               && activity::shared::has_joined_participants(contexts[0],accounts[1].primarySoid,oldHost),
               "empty allocator is reusable; invalid ownership fails closed")) return false;
    const auto oldRevision=worlds[0].stateRevision;
    const auto newRevision=worlds[1].stateRevision;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        bool claimed=false;
        if (!check(group::activity_host_session(returnGroup,40,sources[1],claimed)==0 && claimed
                   && group::held_host_session(returnGroup)==0
                   && group::held_host_session(returnAlias)==0
                   && group::source_activity_session(returnGroup)==sources[1]
                   && worlds[0].sessions[rootSlot].occupied
                   && worlds[0].stateRevision==oldRevision && worlds[1].stateRevision==newRevision,
                   "rebind waits for service without mutating State during a staged push or keeping old alias")) return false;
    }
    sinks[1].reject=true;
    group::allocate_claimed_host_sessions();
    if (!check(group::held_host_session(returnGroup)==0 && !worlds[0].sessions[rootSlot].occupied,
               "service releases old allocator in its account and withholds failed replacement save")) return false;
    sinks[1].reject=false;
    group::allocate_claimed_host_sessions();
    const auto newHost=group::held_host_session(returnGroup);
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        activity::SessionRecord currentSource{},currentHost{};
        if (!check(newHost!=0 && activity::contains(newHost)
                   && group::source_activity_snapshot(returnGroup,currentSource)
                   && currentSource.joined && currentSource.membership.hasIdentity
                   && currentSource.memberKey==0x1122334400000001ULL
                   && currentSource.ownerAccountSoid==accounts[1].primarySoid
                   && activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,newHost,currentHost)
                   && currentHost.destination==currentSource.destination
                   && group::source_activity_session_for_host(newHost)==sources[1],
                   "return admission binds the new authenticated source, account, identity and destination")) return false;
        bool claimed=false;
        if (!check(group::activity_host_session(returnGroup,40,newHost,claimed)==newHost && claimed
                   && group::source_activity_session(returnGroup)==sources[1],
                   "reentry replacement PUBLIC self-advertisement also remains ready")) return false;
    }
    group::reset_host_sessions();
    std::puts("PASS: PUBLIC self-advertisement; account collision, migration alias and canonical guest guards");
    std::puts("PASS: public-region reentry after source retirement; live-source and surviving-guest guards");
    return check(std::memcmp(ownerBefore.data(), &state::runtime::storage::g_state, ownerBefore.size()) == 0,
                 "reentry leaves process owner unchanged");
}

} // namespace restoration::tests
