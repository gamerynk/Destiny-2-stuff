#include "matchmaking_state.h"
#include "social_search.h"
#include "../../middleware/bap/matchmaking/social_search.h"
#include <algorithm>

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../runtime/storage/internal.h"
#include "transactions/internal.h"

namespace dawn::state::matchmaking {

bool acquire_context(ContextHandle& context) noexcept {
    context = {};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    MatchmakingState& state = runtime::storage::g_state.matchmaking;
    if (state.allocatorRevision == kInvalidRevision) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    for (std::size_t index = 0; index < state.contexts.size(); ++index) {
        ContextSlot& slot = state.contexts[index];
        if (slot.active || slot.generation == (std::numeric_limits<std::uint32_t>::max)()) {
            continue;
        }
        // advance before erasure so a released handle cannot become valid again
        std::uint32_t generation = slot.generation;
        ++generation;
        SecureZeroMemory(&slot, sizeof slot);
        slot.active = true;
        slot.generation = generation;
        slot.data.revision = kInitialContextRevision;
        slot.data.latestSlot = kInvalidVariantSlot;
        context = {index, generation};
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return true;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return false;
}

bool release_context(ContextHandle context) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ContextSlot* slot = transactions::resolve(runtime::storage::g_state.matchmaking, context);
    if (slot == nullptr) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    const std::uint32_t generation = slot->generation;
    SecureZeroMemory(slot, sizeof *slot);
    slot->generation = generation;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

bool latest_snapshot(ContextHandle context, LatestSnapshot& snapshot) noexcept {
    SecureZeroMemory(&snapshot, sizeof snapshot);
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    MatchmakingState& state = runtime::storage::g_state.matchmaking;
    ContextSlot* slot = transactions::resolve(state, context);
    if (slot == nullptr) {
        ReleaseSRWLockShared(&runtime::storage::g_stateLock);
        return false;
    }
    LatestSnapshot prepared{};
    if (slot->data.latestSlot < kVariantCapacity) {
        const VariantRecord& latest = slot->data.variants[slot->data.latestSlot];
        if (!latest.occupied || latest.advertisementId == kAbsentAdvertisementId) {
            ReleaseSRWLockShared(&runtime::storage::g_stateLock);
            return false;
        }
        prepared.advertisementId = latest.advertisementId;
        prepared.hasDescriptor = latest.hasDescriptor;
        if (latest.hasDescriptor) {
            std::memcpy(prepared.descriptor.data(), latest.descriptor.data(), kDescriptorSize);
        }
    } else if (slot->data.latestSlot == kInvalidVariantSlot
               && slot->data.standaloneLatestId != kAbsentAdvertisementId) {
        prepared.advertisementId = slot->data.standaloneLatestId;
    } else {
        ReleaseSRWLockShared(&runtime::storage::g_stateLock);
        return false;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    snapshot = prepared;
    // descriptor stays in State and the caller output, not on an extra stack copy
    SecureZeroMemory(&prepared, sizeof prepared);
    return true;
}

void erase_snapshot(LatestSnapshot& snapshot) noexcept {
    SecureZeroMemory(&snapshot, sizeof snapshot);
}

namespace {
bool valid_grant_locked(const ContextSlot& source,const SocialSearchGrant& grant,
                        bool exactRevision) noexcept {
    if(!source.active || source.data.socialAccount!=grant.account || !source.data.socialPrimary
        || grant.account==0 || grant.target==0 || grant.account==grant.target
        || grant.expires<=GetTickCount64() || (exactRevision && grant.expires>GetTickCount64()+120000)
        || grant.session==0 || grant.generation==0
        || grant.partyAccounts!=source.data.socialParty
        || std::find(grant.partyAccounts.begin(),grant.partyAccounts.end(),grant.account)==grant.partyAccounts.end())return false;
    auto* target=transactions::resolve(runtime::storage::g_state.matchmaking,
        {grant.targetSlot,grant.targetGeneration});
    return target && target->data.socialAccount==grant.target && target->data.socialPrimary
        && target->data.socialActivity==source.data.socialActivity
        && target->data.latestSlot<kVariantCapacity
        && target->data.variants[target->data.latestSlot].hasDescriptor
        && target->data.variants[target->data.latestSlot].descriptor==grant.descriptor
        && (!exactRevision || target->data.revision==grant.targetRevision)
        && social_world_matches_locked(grant.target,grant.session,grant.generation,source.data.socialActivity);
}
}

bool prepare_social_search(ContextHandle context,std::uint32_t activity,
                          LatestSnapshot& result,PendingMutation& mutation) noexcept {
    result={};mutation={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    auto& state=runtime::storage::g_state.matchmaking;
    auto* source=transactions::resolve(state,context);
    bool found=false;
    if(source && source->data.socialPrimary && source->data.socialActivity==activity
        && source->data.socialAccount==runtime::storage::account().primarySoid) {
        for(std::size_t i=0;i<state.contexts.size();++i) {
            const auto& target=state.contexts[i];const auto& data=target.data;
            if(&target==source || !target.active || !data.socialPrimary || data.socialActivity!=activity
                || data.socialAccount==source->data.socialAccount || data.latestSlot>=kVariantCapacity)continue;
            const auto& latest=data.variants[data.latestSlot];
            activity::SessionRecord world{};
            if(!latest.occupied || !latest.hasDescriptor || !social_world_locked(data.socialAccount,world)
                || !social_world_matches_locked(data.socialAccount,world.sessionId,world.sharedOwnerCreatedRevision,activity)
                || !world.membership.peerConnection.hasPeer || !world.membership.peerConnection.hasFlags
                || (world.membership.peerConnection.flags&0x10)==0
                || !middleware::bap::matchmaking::social_search::steam_descriptor(latest.descriptor,
                    data.socialPlatform,world.membership.peerConnection.peer))continue;
            // same native session/self results must not cause a join cycle
            if(source->data.latestSlot<kVariantCapacity) {
                const auto& own=source->data.variants[source->data.latestSlot].descriptor;
                namespace descriptor=middleware::bap::matchmaking::social_search;
                const auto online=descriptor::word(latest.descriptor,110);
                const bool sameOnline=online!=0 && descriptor::word(own,110)==online;
                const bool sameNative=descriptor::word(own,0)==descriptor::word(latest.descriptor,0)
                    && std::memcmp(own.data()+8,latest.descriptor.data()+8,86)==0;
                if(sameOnline || sameNative)continue;
            }
            result.advertisementId=latest.advertisementId;result.hasDescriptor=true;result.descriptor=latest.descriptor;
            mutation.kind=MutationKind::socialSearch;mutation.context=context;
            mutation.expectedContextRevision=source->data.revision;
            mutation.socialGrant={source->data.socialAccount,data.socialAccount,world.sessionId,
                world.sharedOwnerCreatedRevision,GetTickCount64()+120000,i,target.generation,data.revision,source->data.socialParty,latest.descriptor};
            found=true;break;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);return found;
}

bool commit_social_search_locked(const PendingMutation& prepared,ContextSlot& source) noexcept {
    if(source.data.socialAccount!=runtime::storage::account().primarySoid
        || !valid_grant_locked(source,prepared.socialGrant,true))return false;
    source.data.socialGrant=prepared.socialGrant;++source.data.revision;return true;
}

bool social_search_native_lobby(std::uint64_t callerAccount,std::uint64_t targetAccount,
    std::uint64_t targetPlatform,std::uint64_t nativeLobby) noexcept {
    if(!callerAccount || !targetAccount || callerAccount==targetAccount || !targetPlatform || !nativeLobby)return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);bool allowed=false;
    for(const auto& source:runtime::storage::g_state.matchmaking.contexts) {
        const auto& grant=source.data.socialGrant;
        if(grant.target!=targetAccount || !valid_grant_locked(source,grant,false)
            || std::find(grant.partyAccounts.begin(),grant.partyAccounts.end(),callerAccount)==grant.partyAccounts.end()
            || middleware::bap::matchmaking::social_search::word(grant.descriptor,110)!=nativeLobby)continue;
        const auto* target=transactions::resolve(runtime::storage::g_state.matchmaking,
            {grant.targetSlot,grant.targetGeneration});
        if(target && target->data.socialPlatform==targetPlatform){allowed=true;break;}
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);return allowed;
}

bool social_search_peers(std::uint64_t first,std::uint64_t second,std::uint64_t session) noexcept {
    if(!first || !second || first==second)return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);bool allowed=false;
    for(const auto& source:runtime::storage::g_state.matchmaking.contexts) {
        const auto& grant=source.data.socialGrant;
        if((session!=0 && grant.session!=session) || !valid_grant_locked(source,grant,false))continue;
        const auto party=[&](std::uint64_t account){return std::find(grant.partyAccounts.begin(),grant.partyAccounts.end(),account)!=grant.partyAccounts.end();};
        if((grant.target==first && party(second)) || (grant.target==second && party(first))){allowed=true;break;}
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);return allowed;
}

} // namespace dawn::state::matchmaking
