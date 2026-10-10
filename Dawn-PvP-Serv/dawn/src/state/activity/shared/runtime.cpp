#include "runtime.h"
#include "public_admission.h"
#include "public_destination_policy.h"
#include "social_admission.h"
#include "../../matchmaking/social_search.h"
#include "../transactions/activity_startup_reservations.h"
#include "../destination/activity_destination_validation.h"
#include "../../runtime/runtime.h"
#include "../../runtime/storage/internal.h"
#include "../transactions/internal.h"
#include "../entity_slots/transactions/internal.h"
#include "../membership/transactions/internal.h"
#include "../../../middleware/content/social_activity_policy.h"
#include <algorithm>
#include <string_view>

namespace dawn::state::activity::shared {
namespace {
std::array<AccountRuntimeContext,64> contexts{};
struct PublicLeaseCensus {
    std::uint64_t session{}, owner{}, created{};
    entity_slots::LeaseMask outstanding{};
};
std::array<PublicLeaseCensus,64*kSessionCapacity> publicLeases{};
struct PublicSource {
    AccountRuntimeContext owner{},caller{};
    std::uint64_t host{},hostCreated{},source{},sourceCreated{},sourceJoined{},account{};
    std::uint8_t slot{};
    membership::Identity admittedIdentity{};
    core::protocol::ActivityPeerConnection admittedConnection{};
};
std::array<PublicSource,64*kSessionCapacity> publicSources{};
struct PeerDependency {
    std::uint64_t source{},sourceCreated{},target{},targetCreated{},owner{};
    struct AdmittedPeer {
        membership::Identity identity{};
        core::protocol::ActivityPeerConnection connection{};
    };
    std::array<AdmittedPeer,32> admitted{};
};
std::array<PeerDependency,64*kSessionCapacity> peerDependencies{};
const SessionRecord* find(const AccountRuntimeContext& context, std::uint64_t id) noexcept {
    if (!context.account || !context.activity) return nullptr;
    const auto slot=transactions::find_session(*context.activity,id);
    if (slot==kInvalidSessionSlot) return nullptr;
    const auto& row=context.activity->sessions[slot];
    return row.ownerAccountSoid==context.account->primarySoid ? &row : nullptr;
}

const SessionRecord* root_locked(std::uint64_t id, AccountRuntimeContext& context,
                                std::uint64_t owner=0,std::uint64_t created=0) noexcept {
    const SessionRecord* found=nullptr;
    for (const auto& candidate:contexts) {
        const auto* row=find(candidate,id);
        if (row && row->sharedOwnerAccountSoid==row->ownerAccountSoid
            && row->sharedLeaseParts>1 && row->membership.identity.memberKey!=0
            && (owner==0 || row->ownerAccountSoid==owner)
            && (created==0 || row->createdRevision==created)) {
            if (found) {context={};return nullptr;}
            context=candidate;
            found=row;
        }
    }
    return found;
}
void reap_public_leases_locked(const ActivityState* replacement=nullptr) noexcept {
    for (auto& lease:publicLeases) {
        if (lease.session==0) continue;
        bool retained=false;
        for (const auto& context:contexts) {
            if (!context.activity) continue;
            const auto& state=replacement && context.activity==&runtime::storage::activity()
                ? *replacement : *context.activity;
            const auto slot=transactions::find_session(state,lease.session);
            if (slot==kInvalidSessionSlot) continue;
            const auto& row=state.sessions[slot];
            retained|=row.occupied && row.publicWorld && row.sharedOwnerAccountSoid==lease.owner
                && row.sharedOwnerCreatedRevision==lease.created;
        }
        if (!retained) lease={};
    }
}
bool same_player(const membership::Identity& a,const membership::Identity& b) noexcept {
    return a.memberKey!=0 && a.accountSoid!=0 && a.opaqueSoid!=0
        && a.joinIdentity!=0 && a.joinIdentity!=membership::kInvalidJoinIdentity
        && a.memberKey==b.memberKey && a.accountSoid==b.accountSoid
        && a.opaqueSoid==b.opaqueSoid && a.joinIdentity==b.joinIdentity;
}
const SessionRecord* public_root_locked(const PublicSource& binding) noexcept {
    const auto* root=find(binding.owner,binding.host);
    return root && root->publicWorld && root->createdRevision==binding.hostCreated
        && root->sharedOwnerAccountSoid==root->ownerAccountSoid
        && root->sharedOwnerCreatedRevision==root->createdRevision ? root : nullptr;
}
const SessionRecord* public_source_locked(const PublicSource& binding) noexcept {
    const auto* host=public_root_locked(binding);
    const auto* source=find(binding.caller,binding.source);
    if(!host
        || !source || !source->joined || source->restoredDormant
        || source->createdRevision!=binding.sourceCreated || !source->membership.hasIdentity
        || source->ownerAccountSoid!=binding.account || source->membership.identity.accountSoid!=binding.account
        || source->memberKey==0 || source->memberKey!=source->membership.identity.memberKey
        || source->membership.identity.opaqueSoid==0 || source->membership.identity.joinIdentity==0
        || source->membership.identity.joinIdentity==membership::kInvalidJoinIdentity
        || !compatible_public_destination(source->destination,host->destination)) return nullptr;
    return source;
}
PublicSource* public_binding_locked(const SessionRecord& host,std::uint64_t account) noexcept {
    PublicSource* found=nullptr;
    for(auto& binding:publicSources)
        if(binding.host==host.sessionId && binding.hostCreated==host.createdRevision
            && binding.owner.account && binding.owner.account->primarySoid==host.ownerAccountSoid
            && binding.account==account) {
            if(found) return nullptr;
            found=&binding;
        }
    return found;
}
void dirty_public_sources_locked(const SessionRecord& source,ActivityState& staged) noexcept {
    for(const auto& binding:publicSources) {
        if(binding.source!=source.sessionId || binding.sourceCreated!=source.createdRevision
            || binding.account!=source.ownerAccountSoid) continue;
        for(const auto& context:contexts) {
            if(!context.activity) continue;
            auto& state=context.activity==&runtime::storage::activity()?staged:*context.activity;
            const auto slot=transactions::find_session(state,binding.host);
            if(slot==kInvalidSessionSlot || state.stateRevision==kMaximumRevision) continue;
            auto& row=state.sessions[slot];
            if(!row.publicWorld || !row.joined || !row.membership.hasIdentity
                || row.sharedOwnerCreatedRevision!=binding.hostCreated
                || row.membership.revision==membership::kMaximumMembershipRevision) continue;
            ++row.membership.revision;row.membership.acknowledgedRevision=0;
            row.recordRevision=++state.stateRevision;
        }
    }
}
bool register_peer_dependency_locked(const SessionRecord& source,const SessionRecord& target) noexcept {
    PeerDependency* free=nullptr;
    for(auto& dep:peerDependencies) {
        if(dep.target==target.sessionId && dep.targetCreated==target.createdRevision
            && dep.owner==target.ownerAccountSoid) {
            return dep.source==source.sessionId && dep.sourceCreated==source.createdRevision;
        }
        if(!free) {
            AccountRuntimeContext ignored{};
            if(dep.target==0 || !root_locked(dep.target,ignored,dep.owner,dep.targetCreated)) free=&dep;
        }
    }
    if(!free) return false;
    *free={source.sessionId,source.createdRevision,target.sessionId,target.createdRevision,target.ownerAccountSoid};
    return true;
}
PeerDependency* dependency_locked(std::uint64_t sourceId,const SessionRecord& target) noexcept {
    for(auto& dep:peerDependencies)
        if(dep.source==sourceId && dep.target==target.sessionId
            && dep.owner==target.ownerAccountSoid && dep.targetCreated==target.createdRevision) return &dep;
    return nullptr;
}
void dirty_peer_dependents_locked(const SessionRecord& source,ActivityState* staged=nullptr) noexcept {
    for(const auto& dep:peerDependencies) {
        if(dep.source!=source.sessionId || dep.owner!=source.sharedOwnerAccountSoid
            || dep.sourceCreated!=source.sharedOwnerCreatedRevision) continue;
        for(const auto& context:contexts) {
            if(!context.activity) continue;
            auto& state=staged && context.activity==&runtime::storage::activity()?*staged:*context.activity;
            const auto slot=transactions::find_session(state,dep.target);
            if(slot==kInvalidSessionSlot || state.stateRevision==kMaximumRevision) continue;
            auto& row=state.sessions[slot];
            if(!row.joined || !row.membership.hasIdentity || row.sharedOwnerAccountSoid!=dep.owner
                || row.sharedOwnerCreatedRevision!=dep.targetCreated
                || row.membership.revision==membership::kMaximumMembershipRevision) continue;
            ++row.membership.revision;row.membership.acknowledgedRevision=0;
            row.recordRevision=++state.stateRevision;
        }
    }
}
void map_peer_transport_locked(std::uint64_t sourceId,const SessionRecord& target,Projection& output) noexcept {
    const auto publicConnections=output.peerConnections;
    output.peerConnections={}; // a failed binding must never leak a server address
    if(target.publicWorld) {
        for(std::size_t slot=0;slot<output.members.size();++slot) {
            if((output.joinedMemberMask&(std::uint32_t{1}<<slot))==0) continue;
            auto* binding=public_binding_locked(target,output.members[slot].accountSoid);
            if(!binding || binding->slot!=slot) continue;
            const auto* source=public_source_locked(*binding);
            const auto* current=find(binding->caller,binding->source);
            if(source) {
                if(!same_player(source->membership.identity,output.members[slot])) continue;
                binding->admittedIdentity=source->membership.identity;
                binding->admittedConnection=source->membership.peerConnection;
            } else if(current && current->joined) continue;
            if(!same_player(binding->admittedIdentity,output.members[slot])) continue;
            auto connection=binding->admittedConnection;
            const auto kind=std::to_integer<unsigned>(connection.peer.back());
            if(!connection.hasPeer || !connection.hasProxy || (kind!=6 && kind!=7)) continue;
            const bool ready=!source || (connection.hasFlags && (connection.flags&0x10)!=0);
            connection.flags=publicConnections[slot].flags;
            connection.hasFlags=publicConnections[slot].hasFlags;
            if(!ready) connection.flags&=static_cast<std::uint8_t>(~0x10U);
            output.peerConnections[slot]=connection;
        }
        return;
    }
    AccountRuntimeContext owner{};
    const auto* root=root_locked(sourceId,owner,target.ownerAccountSoid);
    if(sourceId==target.sessionId) return;
    if(root && (!root->membership.hasIdentity
        || root->destination.activityIndex!=target.destination.activityIndex
        || !register_peer_dependency_locked(*root,target))) return;
    auto* dep=dependency_locked(sourceId,target);
    if(!dep || (root && dep->sourceCreated!=root->createdRevision)) return;
    std::uint32_t assigned=0,duplicates=0;
    for(const auto& context:contexts) {
        const auto* source=find(context,sourceId);
        const auto* peer=find(context,target.sessionId);
        if(!peer || !peer->joined || !peer->membership.hasIdentity
            || peer->sharedOwnerAccountSoid!=target.ownerAccountSoid
            || peer->sharedOwnerCreatedRevision!=target.createdRevision || peer->sharedMemberSlot>=32) continue;
        const auto slot=peer->sharedMemberSlot;
        const auto mask=std::uint32_t{1}<<slot;
        if((output.joinedMemberMask&mask)==0 || !same_player(output.members[slot],peer->membership.identity)) continue;
        if((assigned&mask)!=0) {duplicates|=mask;output.peerConnections[slot]={};continue;}
        assigned|=mask;
        if((duplicates&mask)!=0) continue;
        auto& admitted=dep->admitted[slot];
        const bool liveSource=source && source->joined;
        if(liveSource) {
            if(!root || !source->membership.hasIdentity
                || source->sharedOwnerAccountSoid!=dep->owner
                || source->sharedOwnerCreatedRevision!=dep->sourceCreated
                || !same_player(output.members[slot],source->membership.identity)) {
                admitted={};continue;
            }
            admitted={source->membership.identity,source->membership.peerConnection};
        }
        if(!same_player(admitted.identity,output.members[slot])) continue;
        auto connection=admitted.connection;
        const auto kind=std::to_integer<unsigned>(connection.peer.back());
        if(!connection.hasPeer || !connection.hasProxy || (kind!=6 && kind!=7)) continue;
        const bool sourceReady=!liveSource || (connection.hasFlags && (connection.flags&0x10)!=0);
        connection.flags=publicConnections[slot].flags;
        connection.hasFlags=publicConnections[slot].hasFlags;
        if(!sourceReady) connection.flags&=static_cast<std::uint8_t>(~0x10U);
        output.peerConnections[slot]=connection;
    }
}
bool prepare_locked(const AccountRuntimeContext& source, std::uint64_t id, std::uint64_t key,
                    std::uint64_t character, std::size_t requested, std::size_t reserve,
                    entity_slots::PendingMutation& output, JoinFailure* failure=nullptr,
                    const membership::Identity* authorizedPrivateIdentity=nullptr) noexcept {
    output={};
    if (failure) *failure=JoinFailure::validation;
    const auto* root=find(source,id);
    const auto account=runtime::storage::account().primarySoid;
    auto& state=runtime::storage::activity();
    const auto existing=transactions::find_session(state,id);
    const bool dormantPublicImport=root && (root->publicWorld || root->socialWorld) && existing!=kInvalidSessionSlot
        && state.sessions[existing].restoredDormant
        && state.sessions[existing].ownerAccountSoid==account && !state.sessions[existing].publicWorld;
    if (!root || root->membership.identity.memberKey==0 || key==0 || character==0
        || root->ownerAccountSoid==account || root->sharedOwnerAccountSoid!=root->ownerAccountSoid
        || root->sharedOwnerCreatedRevision!=root->createdRevision || root->sharedLeaseParts<2
        || root->recordRevision==kInvalidRevision || requested==0 || reserve>=entity_slots::kSlotCount
        || requested>entity_slots::kSlotCount-reserve || state.stateRevision==kMaximumRevision
        || (existing!=kInvalidSessionSlot && !dormantPublicImport)
        || (root->publicWorld && !valid_public_join_locked(*root,account,key,character))) return false;
    std::size_t member=32;
    for (std::size_t i=0;i<root->membership.reservations.size();++i) {
        const auto& peer=root->membership.reservations[i];
        if (peer.memberKey==key && peer.accountSoid==account && peer.opaqueSoid==character) member=i+1;
    }
    if (failure) *failure=JoinFailure::reservation;
    if (authorizedPrivateIdentity) {
        const auto& peer=*authorizedPrivateIdentity;
        if(root->publicWorld || root->socialWorld || root->restoredDormant || !root->joined
            || root->sharedOwnerDetached || !root->membership.hasIdentity
            || root->sharedLeaseParts<kPrivateLeaseParts
            || peer.memberKey!=key || peer.accountSoid!=account || peer.opaqueSoid!=character
            || peer.smallOpaque!=-1 || peer.joinIdentity==0
            || peer.joinIdentity==membership::kInvalidJoinIdentity) return false;
        std::uint32_t occupied=1;
        for(std::size_t i=0;i<root->membership.reservations.size();++i) {
            const auto& reserved=root->membership.reservations[i];
            if(reserved.memberKey==0)continue;
            if((reserved.accountSoid==account || reserved.memberKey==key)
                && (reserved.accountSoid!=account || reserved.memberKey!=key
                    || reserved.opaqueSoid!=character)) return false;
            occupied|=std::uint32_t{1}<<(i+1);
        }
        std::uint32_t joined=1;
        for(const auto& context:contexts) {
            const auto* row=find(context,id);
            if(!row || !row->joined || row->sharedOwnerAccountSoid!=root->ownerAccountSoid
                || row->sharedOwnerCreatedRevision!=root->createdRevision)continue;
            if(row->sharedMemberSlot>=root->sharedLeaseParts || row->sharedMemberSlot>=32
                || row->ownerAccountSoid==account || row->memberKey==key) return false;
            joined|=std::uint32_t{1}<<row->sharedMemberSlot;
        }
        if(member<root->sharedLeaseParts) {
            if((joined&(std::uint32_t{1}<<member))!=0)return false;
        } else {
            occupied|=joined;
            for(std::size_t i=1;i<root->sharedLeaseParts && i<32;++i)
                if((occupied&(std::uint32_t{1}<<i))==0){member=i;break;}
        }
    }
    if (member>=root->sharedLeaseParts) return false;
    for(const auto& context:contexts) {
        const auto* row=find(context,id);
        if(row && row->joined && row->sharedOwnerAccountSoid==root->ownerAccountSoid
            && row->sharedOwnerCreatedRevision==root->createdRevision
            && (row->sharedMemberSlot==member || row->ownerAccountSoid==account
                || row->memberKey==key)) return false;
    }
    const auto target=dormantPublicImport?existing:transactions::select_shared_target(state,account);
    if (failure) *failure=JoinFailure::capacity;
    if (target==kInvalidSessionSlot) return false;
    const auto reserved=entity_slots::transactions::reserve_high(reserve);
    if (failure) *failure=JoinFailure::reserve;
    if (root->joined && reserved!=root->serverEntitySlots) return false;
    auto& record=output.sharedRecord;
    record.destination=root->destination;
    record.sessionId=id;
    record.ownerAccountSoid=account;
    record.sharedOwnerAccountSoid=root->ownerAccountSoid;
    record.sharedOwnerCreatedRevision=root->createdRevision;
    record.sharedMemberSlot=static_cast<std::uint8_t>(member);
    record.sharedLeaseParts=root->sharedLeaseParts;
    record.publicWorld=root->publicWorld;
    record.socialWorld=root->socialWorld;
    record.membership.reservations=root->membership.reservations;
    record.membership.identity=authorizedPrivateIdentity
        ?as_reservation(*authorizedPrivateIdentity):root->membership.reservations[member-1];
    record.membership.reservations[member-1]=root->membership.hasIdentity
        ?as_reservation(root->membership.identity):root->membership.identity;
    output.mask=entity_slots::transactions::select_free(allocation_exclusions_locked(record,reserve,true),requested);
    if (failure) *failure=JoinFailure::lease;
    if (entity_slots::transactions::empty(output.mask)) return false;
    output.serverMask=reserved;
    output.sessionId=id; output.memberKey=key; output.expectedMemberKey=key;
    output.expectedStateRevision=state.stateRevision; output.requestedCount=requested;
    output.serverReserveCount=reserve; output.targetSlot=target;
    output.sharedSource=source; output.sharedSourceRevision=root->recordRevision;
    output.sharedSourceCreatedRevision=root->createdRevision; output.sharedCharacterSoid=character;
    if(authorizedPrivateIdentity) {
        output.privateAdmission=*authorizedPrivateIdentity;
        output.privateAdmissionGuard=*authorizedPrivateIdentity;
        output.admitsPrivatePeer=true;
    }
    output.kind=entity_slots::MutationKind::join; output.prepared=true; output.importsShared=true;
    if (failure) *failure=JoinFailure::none;
    return true;
}
}
bool compatible_public_destination(const destination::DestinationSelection& a,
                                   const destination::DestinationSelection& b) noexcept {
    if(a.activityIndex<0 || a.activityIndex!=b.activityIndex
        || a.packageNameLength==0 || a.packageNameLength>a.packageName.size()
        || a.packageNameLength!=b.packageNameLength || a.packageName!=b.packageName) return false;
    const std::string_view name(reinterpret_cast<const char*>(a.packageName.data()),a.packageNameLength);
    return public_destination_package(name);
}
bool social_destination(const destination::DestinationSelection& destination) noexcept {
    if(destination.activityIndex<0 || destination.packageNameLength==0
        || destination.packageNameLength>destination.packageName.size()) return false;
    const std::string_view name(reinterpret_cast<const char*>(destination.packageName.data()),destination.packageNameLength);
    return name=="city_tower_social_d2" || name=="campaign_social_space_d2"
        || name=="tangled_shore_social_space";
}
namespace {
bool social_root(const SessionRecord& root) noexcept {
    return root.occupied && root.socialWorld && !root.publicWorld && !root.restoredDormant
        && root.sharedOwnerAccountSoid==root.ownerAccountSoid
        && root.sharedOwnerCreatedRevision==root.createdRevision
        && root.sharedLeaseParts==kPublicLeaseParts && root.membership.identity.memberKey!=0;
}
// joined identities are native byte order; admission reservations are wire-word order
bool social_reservations_locked(const SessionRecord& root,const PendingAllocation& plan,
                                membership::Reservations& merged) noexcept {
    if(!social_root(root) || !compatible_public_destination(root.destination,plan.destination)) return false;
    merged=root.membership.reservations;
    std::array<membership::Identity,32> incoming{};
    incoming[0]=plan.startupOwner;
    std::copy(plan.startupReservations.begin(),plan.startupReservations.end(),incoming.begin()+1);
    for(const auto& id:incoming) {
        if(id.memberKey==0) continue;
        for(const auto& context:contexts) {
            const auto* peer=find(context,root.sessionId);
            if(peer && peer->ownerAccountSoid==id.accountSoid && peer->joined && !peer->sharedOwnerDetached
                && peer->sharedOwnerAccountSoid==root.ownerAccountSoid
                && peer->sharedOwnerCreatedRevision==root.createdRevision) return false;
        }
        if(id.accountSoid==root.ownerAccountSoid) {
            const auto owner=root.membership.hasIdentity?as_reservation(root.membership.identity):root.membership.identity;
            if(!root.sharedOwnerDetached && !membership::transactions::equal(owner,id)) return false;
            for(const auto& reserved:merged)
                if(reserved.memberKey==id.memberKey) return false;
            continue;
        }
        std::size_t slot=merged.size();
        for(std::size_t i=0;i<merged.size();++i) {
            const auto& row=merged[i];
            if(row.memberKey==id.memberKey && row.accountSoid!=id.accountSoid) return false;
            if(row.accountSoid==id.accountSoid && row.memberKey!=0) {slot=i;break;}
        }
        if(slot==merged.size()) for(std::size_t i=0;i<merged.size();++i)
            if(merged[i].memberKey==0) {slot=i;break;}
        if(slot==merged.size() || id.memberKey==root.membership.identity.memberKey) return false;
        merged[slot]=id;
    }
    return true;
}
const SessionRecord* social_target_locked(const PendingAllocation& plan,AccountRuntimeContext& owner) noexcept {
    owner={};
    for(const auto& context:contexts) {
        if(!context.account || !context.activity) continue;
        for(const auto& row:context.activity->sessions) {
            membership::Reservations merged{};
            if(row.ownerAccountSoid==context.account->primarySoid
                && social_reservations_locked(row,plan,merged)) {owner=context;return &row;}
        }
    }
    return nullptr;
}
bool social_plan_valid(const PendingAllocation& plan) noexcept {
    return plan.prepared && plan.socialWorld && plan.socialWorldGuard && plan.publicLeaseParts==0
        && plan.publicLeasePartsGuard==0 && social_destination(plan.destination)
        && destination::valid(plan.destination) && plan.targetSlot<kSessionCapacity
        && plan.selectedSessionCounter!=0 && plan.sessionId!=0
        && plan.sessionId==transactions::compose_session_soid(plan.soidBase,plan.selectedSessionCounter)
        && plan.startupOwner.memberKey!=0 && plan.startupOwner.opaqueSoid!=0
        && transactions::valid_startup_reservations(plan);
}
}
bool prepare_social_allocation(PendingAllocation& plan) noexcept {
    if(!plan.prepared || !social_destination(plan.destination) || plan.startupOwner.memberKey==0
        || plan.startupOwner.accountSoid!=runtime::storage::account().primarySoid
        || !transactions::valid_startup_reservations(plan) || plan.publicLeaseParts!=0) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    AccountRuntimeContext owner{};
    const auto* root=social_target_locked(plan,owner);
    plan.socialWorld=plan.socialWorldGuard=true;
    if(root) {
        plan.socialSessionId=root->sessionId;plan.socialOwner=root->ownerAccountSoid;
        plan.socialCreated=root->createdRevision;plan.socialRevision=root->recordRevision;
    } else {
        plan.selectedSessionCounter=available_session_counter_locked(plan.soidBase,plan.expectedNextSessionId,true);
        plan.sessionId=transactions::compose_session_soid(plan.soidBase,plan.selectedSessionCounter);
    }
    const bool ok=plan.selectedSessionCounter!=0;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool social_allocation_valid_locked(const PendingAllocation& plan) noexcept {
    AccountRuntimeContext owner{};
    return social_plan_valid(plan) && plan.socialSessionId==0 && !social_target_locked(plan,owner);
}
bool commit_social_allocation(const PendingAllocation& plan) noexcept {
    if(!social_plan_valid(plan) || plan.socialSessionId==0 || plan.socialCreated==0 || plan.socialRevision==0)
        return false;
    std::uint64_t account{};
    const auto caller=capture_account_runtime(account);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const auto& state=runtime::storage::activity();
    AccountRuntimeContext owner{};
    const auto* root=root_locked(plan.socialSessionId,owner,plan.socialOwner,plan.socialCreated);
    membership::Reservations merged{};
    bool ok=account==plan.ownerAccountSoid && context_available_locked(caller)
        && state.stateRevision==plan.expectedStateRevision && state.allocatorRevision==plan.expectedAllocatorRevision
        && state.nextSessionId==plan.expectedNextSessionId && root && root->recordRevision==plan.socialRevision
        && owner.activity->stateRevision<kMaximumRevision
        && root->membership.revision<membership::kMaximumMembershipRevision
        && social_reservations_locked(*root,plan,merged);
    if(ok) {
        const ScopedAccountRuntime scope(owner);
        ActivityState next=*owner.activity;
        auto& target=next.sessions[transactions::find_session(next,plan.socialSessionId)];
        target.membership.reservations=merged;
        if(target.sharedOwnerDetached) {
            const auto renew=[&](const membership::Identity& id) {
                if(id.memberKey!=0 && id.accountSoid==target.ownerAccountSoid) {
                    target.membership.identity=target.membership.hasIdentity?as_reservation(id):id;
                    target.memberKey=id.memberKey;
                }
            };
            renew(plan.startupOwner);
            for(const auto& id:plan.startupReservations) renew(id);
        }
        ++target.membership.revision;target.membership.acknowledgedRevision=0;
        target.recordRevision=++next.stateRevision;
        ok=runtime::storage::publish_activity_locked(next);
        if(ok) register_context_locked(caller);
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
bool social_join_authorized(std::uint64_t session,std::uint64_t key,std::uint64_t character) noexcept {
    if(session==0 || key==0 || character==0) return false;
    const auto account=account_snapshot().primarySoid;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    AccountRuntimeContext owner{};
    const auto* root=root_locked(session,owner);
    bool ok=false;
    if(root && social_root(*root)) {
        const auto matches=[&](const membership::Identity& id) {
            return id.accountSoid==account && id.memberKey==key && id.opaqueSoid==character;
        };
        ok=matches(root->membership.identity);
        for(const auto& id:root->membership.reservations) ok=ok || matches(id);
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool social_session(std::uint64_t session) noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    AccountRuntimeContext owner{};const auto* root=root_locked(session,owner);
    const bool ok=root && social_root(*root);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);return ok;
}
std::uint64_t available_session_counter_locked(std::uint64_t base,std::uint64_t first,bool publicAllocation) noexcept {
    auto counter=first;
    for(std::size_t attempt=0;attempt<=contexts.size()*kSessionCapacity;++attempt) {
        if(counter==0) return 0;
        const auto id=transactions::compose_session_soid(base,counter);
        bool occupied=false;
        for(const auto& context:contexts) {
            const auto* row=find(context,id);
            if(row && (publicAllocation || row->publicWorld || row->socialWorld)) {occupied=true;break;}
        }
        if(!occupied) return counter;
        if(counter==kMaximumSessionId) return 0;
        ++counter;
    }
    return 0;
}
bool prepare_public_allocator(PendingAllocation& allocation) noexcept {
    if(!allocation.prepared || allocation.publicLeaseParts!=kPublicLeaseParts
        || allocation.publicLeasePartsGuard!=kPublicLeaseParts) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto counter=available_session_counter_locked(allocation.soidBase,allocation.expectedNextSessionId,true);
    const bool ok=counter!=0 && runtime::storage::activity().stateRevision==allocation.expectedStateRevision
        && runtime::storage::account().primarySoid==allocation.ownerAccountSoid;
    if(ok) {allocation.selectedSessionCounter=counter;allocation.sessionId=transactions::compose_session_soid(allocation.soidBase,counter);}
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool admit_public_source(const AccountRuntimeContext& owner,std::uint64_t hostId,
                         const AccountRuntimeContext& caller,std::uint64_t sourceId) noexcept {
    if(!owner.account || !owner.activity || !caller.account || !caller.activity || hostId==0 || sourceId==0)
        return false;
    const ScopedAccountRuntime scope(owner);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const auto* root=find(owner,hostId);
    const auto* source=find(caller,sourceId);
    bool ok=root && root->publicWorld && root->sharedOwnerAccountSoid==root->ownerAccountSoid
        && root->sharedOwnerCreatedRevision==root->createdRevision && root->sharedLeaseParts==kPublicLeaseParts
        && source && source->joined && !source->restoredDormant && source->membership.hasIdentity
        && source->membership.identity.accountSoid==source->ownerAccountSoid
        && source->membership.identity.memberKey==source->memberKey && source->memberKey!=0
        && source->membership.identity.opaqueSoid!=0 && source->membership.identity.joinIdentity!=0
        && source->membership.identity.joinIdentity!=membership::kInvalidJoinIdentity
        && compatible_public_destination(source->destination,root->destination);
    PublicSource* binding=nullptr;
    std::size_t member=32;
    if(ok) {
        binding=public_binding_locked(*root,source->ownerAccountSoid);
        const auto* existing=find(caller,hostId);
        if(existing && existing->joined && binding
            && (binding->source!=sourceId || binding->sourceCreated!=source->createdRevision)) ok=false;
        if(source->ownerAccountSoid==root->ownerAccountSoid) member=0;
        else {
            for(std::size_t i=0;i<root->membership.reservations.size();++i) {
                const auto& row=root->membership.reservations[i];
                if(row.accountSoid==source->ownerAccountSoid && row.memberKey==source->memberKey) {member=i+1;break;}
            }
            if(member==32) for(std::size_t i=0;i<root->membership.reservations.size();++i)
                if(root->membership.reservations[i].memberKey==0) {member=i+1;break;}
        }
        if(!binding) for(auto& candidate:publicSources) {
            if(candidate.host==0 || !public_root_locked(candidate)) {binding=&candidate;break;}
        }
        ok=ok && binding && member<root->sharedLeaseParts;
    }
    if(ok) {
        const auto proposed=as_reservation(source->membership.identity);
        const bool changed=member!=0
            && !membership::transactions::equal(root->membership.reservations[member-1],proposed);
        if(changed) {
            auto& state=*owner.activity;
            if(state.stateRevision==kMaximumRevision || root->membership.revision==membership::kMaximumMembershipRevision)
                ok=false;
            else {
                ActivityState next=state;
                auto& row=next.sessions[transactions::find_session(next,hostId)];
                row.membership.reservations[member-1]=proposed;
                ++row.membership.revision;row.membership.acknowledgedRevision=0;
                row.recordRevision=++next.stateRevision;
                ok=runtime::storage::publish_activity_locked(next);
                root=find(owner,hostId);
            }
        }
        if(ok) {
            *binding={owner,caller,hostId,root->createdRevision,sourceId,source->createdRevision,source->joinedRevision,
                source->ownerAccountSoid,static_cast<std::uint8_t>(member),
                source->membership.identity,source->membership.peerConnection};
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
bool public_source_snapshot(std::uint64_t host,std::uint64_t account,SessionRecord& source) noexcept {
    source={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool found=false;
    for(const auto& binding:publicSources) {
        if(binding.host!=host || binding.account!=account) continue;
        const auto* row=public_source_locked(binding);
        if(!row) continue;
        if(found) {found=false;source={};break;}
        source=*row;found=true;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}
bool public_source_snapshots(std::uint64_t host,std::array<SessionRecord,32>& sources) noexcept {
    sources={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool found=false,ok=true;
    for(const auto& binding:publicSources) {
        if(binding.host!=host) continue;
        const auto* row=public_source_locked(binding);
        if(!row) continue;
        if(binding.slot>=sources.size() || sources[binding.slot].occupied) {ok=false;break;}
        sources[binding.slot]=*row;found=true;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if(!ok) sources={};
    return ok && found;
}
bool public_join_authorized(std::uint64_t host,std::uint64_t key,std::uint64_t character) noexcept {
    SessionRecord source{};
    return public_source_snapshot(host,state::account_snapshot().primarySoid,source)
        && source.memberKey==key && source.membership.identity.opaqueSoid==character;
}
static bool public_member_connection_impl(std::uint64_t host,std::uint64_t memberKey,std::uint64_t playerId,
                              core::protocol::ActivityPeerConnection& output,
                              const PublicMemberConnectionFence* expected) noexcept {
    output={};
    if(host==0 || memberKey==0 || playerId==0) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool found=false;
    for(const auto& binding:publicSources) {
        if(binding.host!=host || binding.admittedIdentity.memberKey!=memberKey
            || as_reservation(binding.admittedIdentity).joinIdentity!=playerId) continue;
        if(expected && !public_connection_fence_matches(binding,*expected)) continue;
        if(!public_root_locked(binding)) continue;
        const auto* source=public_source_locked(binding);
        auto connection=binding.admittedConnection;
        if(source) {
            if((expected && source->joinedRevision!=expected->joined)
                || !same_player(source->membership.identity,binding.admittedIdentity)) continue;
            connection=source->membership.peerConnection;
        } else {
            const auto* current=find(binding.caller,binding.source);
            const auto* participant=find(binding.caller,host);
            if(!retained_public_connection_allowed(current,participant,binding.hostCreated,binding.slot,
                participant && same_player(participant->membership.identity,binding.admittedIdentity))) continue;
        }
        const auto method=std::to_integer<unsigned>(connection.peer.back());
        if(!connection.hasPeer || !connection.hasProxy || !connection.hasFlags
            || (connection.flags&0x10)==0 || (method!=6 && method!=7)) continue;
        if(found) {output={};found=false;break;}
        output=connection;found=true;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}
bool public_member_connection(std::uint64_t host,std::uint64_t memberKey,std::uint64_t playerId,
                              core::protocol::ActivityPeerConnection& output) noexcept {
    return public_member_connection_impl(host,memberKey,playerId,output,nullptr);
}
bool public_member_connection(std::uint64_t host,std::uint64_t memberKey,std::uint64_t playerId,
                              core::protocol::ActivityPeerConnection& output,
                              const PublicMemberConnectionFence& expected) noexcept {
    return public_member_connection_impl(host,memberKey,playerId,output,&expected);
}
bool valid_public_join_locked(const SessionRecord& host,std::uint64_t account,std::uint64_t key,
                              std::uint64_t character) noexcept {
    const auto* binding=public_binding_locked(host,account);
    const auto* source=binding?public_source_locked(*binding):nullptr;
    return source && source->memberKey==key
        && (character==0 || source->membership.identity.opaqueSoid==character);
}
bool public_peers(std::uint64_t firstAccount,std::uint64_t secondAccount) noexcept {
    if(firstAccount==0 || secondAccount==0 || firstAccount==secondAccount) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool ok=false;
    for(const auto& context:contexts) {
        if(!context.account || context.account->primarySoid!=firstAccount || !context.activity) continue;
        for(const auto& first:context.activity->sessions) {
            if(!first.socialWorld || !first.joined || !first.membership.hasIdentity || first.sharedOwnerDetached) continue;
            AccountRuntimeContext owner{};
            const auto* root=root_locked(first.sessionId,owner,first.sharedOwnerAccountSoid,first.sharedOwnerCreatedRevision);
            if(!root || !social_root(*root)) continue;
            for(const auto& other:contexts) {
                if(!other.account || other.account->primarySoid!=secondAccount) continue;
                const auto* second=find(other,first.sessionId);
                if(second && second->socialWorld && second->joined && second->membership.hasIdentity
                    && !second->sharedOwnerDetached && second->sharedOwnerAccountSoid==root->ownerAccountSoid
                    && second->sharedOwnerCreatedRevision==root->createdRevision) ok=true;
            }
        }
    }
    for(const auto& binding:publicSources) {
        if(binding.account!=firstAccount) continue;
        const auto* first=find(binding.caller,binding.host);
        const auto* root=find(binding.owner,binding.host);
        if(!first || !root || !root->publicWorld || !first->joined || !first->membership.hasIdentity
            || first->sharedOwnerCreatedRevision!=binding.hostCreated || first->sharedOwnerDetached) continue;
        const auto* other=public_binding_locked(*root,secondAccount);
        const auto* second=other?find(other->caller,binding.host):nullptr;
        if(second && second->joined && second->membership.hasIdentity && !second->sharedOwnerDetached
            && second->sharedOwnerCreatedRevision==binding.hostCreated) {ok=true;break;}
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool canonical_absent(std::uint64_t id) noexcept {
 if(!id)return false;
 AcquireSRWLockShared(&runtime::storage::g_stateLock);
 bool absent=true;
 for(const auto& context:contexts)if(context.activity)
  for(const auto& row:context.activity->sessions)if(row.occupied&&row.sessionId==id)absent=false;
 for(const auto& row:runtime::storage::activity().sessions)if(row.occupied&&row.sessionId==id)absent=false;
 for(const auto& row:publicSources)if(row.source==id||row.host==id)absent=false;
 for(const auto& row:peerDependencies)if(row.source==id||row.target==id)absent=false;
 for(const auto& row:publicLeases)if(row.session==id)absent=false;
 ReleaseSRWLockShared(&runtime::storage::g_stateLock);return absent;
}
bool context_available_locked(const AccountRuntimeContext& context) noexcept {
    if (!context.account || !context.activity) return false;
    for (const auto& row:contexts)
        if (!row.activity || (row.account==context.account && row.activity==context.activity)) return true;
    return false;
}
void register_context_locked(const AccountRuntimeContext& context) noexcept {
    for (const auto& row:contexts)
        if (row.account==context.account && row.activity==context.activity) return;
    for (auto& row:contexts) if (!row.activity) {row=context;return;}
}
void reset_locked() noexcept {contexts={};publicLeases.fill({});peerDependencies.fill({});publicSources.fill({});entity_retirement::reset_locked();}
bool valid_identity(const SessionRecord& record,const membership::Identity& identity) noexcept {
    return record.sharedOwnerAccountSoid==0
        || (identity.memberKey==record.memberKey && identity.accountSoid==record.ownerAccountSoid
            && identity.opaqueSoid==record.membership.identity.opaqueSoid);
}
namespace {
bool disconnect_exact(const AccountRuntimeContext& context,std::uint64_t id,
                      std::uint64_t expectedMemberKey,bool retainEmptyAnchor=false,
                      std::uint64_t expectedJoinRevision=0) noexcept {
    if (id==0 || !context.account || !context.activity) return false;
    const ScopedAccountRuntime scope(context);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    if (expectedJoinRevision!=0 && !entity_retirement::matches_locked(
        id,context.account->primarySoid,expectedJoinRevision)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false; // an old BAP EOF must not disconnect a newer ActivityClient incarnation
    }
    bool disconnected=false;
    auto& current=runtime::storage::activity();
    const auto slot=transactions::find_session(current,id);
    if (slot!=kInvalidSessionSlot) {
        const auto binding=current.sessions[slot];
        note_slot_lease_locked(binding,binding.heldEntitySlots);
        // a PRIVATE startup with a rejected party reservation still gets a local join. Leaving it
        // joined pins the region's cached source forever, so only the journalled connection retires it
        if (binding.ownerAccountSoid==context.account->primarySoid
            && binding.sharedOwnerAccountSoid==0 && binding.joined
            && expectedJoinRevision!=0 && expectedMemberKey==0
            && current.stateRevision<kMaximumRevision) {
            if (binding.membership.hasIdentity && binding.memberKey!=0)
                entity_retirement::depart_locked(id,binding.ownerAccountSoid,binding.memberKey,
                                                  binding.heldEntitySlots);
            dirty_peer_dependents_locked(binding);
            current.sessions[slot]={};
            ++current.stateRevision;
            disconnected=true;
        }
        if (binding.ownerAccountSoid==context.account->primarySoid && binding.sharedOwnerAccountSoid!=0
            && !binding.sharedOwnerDetached
            && (expectedMemberKey==0 || (binding.joined && binding.membership.hasIdentity
                && binding.memberKey==expectedMemberKey
                && binding.membership.identity.memberKey==expectedMemberKey))) {
            disconnected=true;
            if (binding.joined && binding.membership.hasIdentity && binding.memberKey!=0) {
                // PUBLIC id15 arrives before native26; capture ownership before clearing the joined record
                entity_retirement::depart_locked(id,binding.ownerAccountSoid,binding.memberKey,
                                                  binding.heldEntitySlots);
            }
            dirty_peer_dependents_locked(binding);
            const bool owner=binding.sharedOwnerAccountSoid==binding.ownerAccountSoid;
            if(owner) {
                auto& anchor=current.sessions[slot];
                anchor.joined=false;anchor.sharedOwnerDetached=true;
                anchor.heldEntitySlots={};anchor.membership.peerConnection={};
                anchor.membership.transitionToken=0;anchor.membership.hasTransitionToken=false;
                if(current.stateRevision<kMaximumRevision) anchor.recordRevision=++current.stateRevision;
            } else {
                current.sessions[slot]={};
                if(current.stateRevision<kMaximumRevision) ++current.stateRevision;
            }
            for(auto& dep:peerDependencies)
                if(dep.target==id && dep.owner==binding.sharedOwnerAccountSoid
                    && dep.targetCreated==binding.sharedOwnerCreatedRevision && binding.sharedMemberSlot<32)
                    dep.admitted[binding.sharedMemberSlot]={};
            bool anyJoined=false;
            for (const auto& registered:contexts) {
                if (!registered.activity) continue;
                auto& state=*registered.activity;
                const auto index=transactions::find_session(state,id);
                if (index==kInvalidSessionSlot) continue;
                auto& peer=state.sessions[index];
                if (peer.sharedOwnerAccountSoid==binding.sharedOwnerAccountSoid
                    && peer.sharedOwnerCreatedRevision==binding.sharedOwnerCreatedRevision) {
                    anyJoined|=peer.joined;
                    bool retiredReservation=false;
                    // keep this identity-only row: native C17A04 otherwise waits forever on an inactive peer
                    // before acking the visitor's rejoin cleanup
                    if(binding.socialWorld && binding.joined && binding.membership.hasIdentity
                        && binding.sharedMemberSlot>0 && binding.sharedMemberSlot<32) {
                        auto& reservation=peer.membership.reservations[binding.sharedMemberSlot-1];
                        if(reservation.memberKey==binding.memberKey
                            && reservation.accountSoid==binding.ownerAccountSoid
                            && reservation.opaqueSoid==binding.membership.identity.opaqueSoid) {
                            reservation={};retiredReservation=true;
                        }
                    }
                    if((peer.joined || retiredReservation) && state.stateRevision<kMaximumRevision
                        && peer.membership.revision<membership::kMaximumMembershipRevision) {
                        ++peer.membership.revision;peer.membership.acknowledgedRevision=0;
                        peer.recordRevision=++state.stateRevision;
                    }
                }
            }
            if(!anyJoined && !retainEmptyAnchor) {
                for(const auto& registered:contexts) {
                    if(!registered.activity) continue;
                    auto& state=*registered.activity;
                    const auto index=transactions::find_session(state,id);
                    if(index==kInvalidSessionSlot) continue;
                    auto& peer=state.sessions[index];
                    if(peer.sharedOwnerDetached && peer.sharedOwnerAccountSoid==binding.sharedOwnerAccountSoid
                        && peer.sharedOwnerCreatedRevision==binding.sharedOwnerCreatedRevision) {
                        peer={};if(state.stateRevision<kMaximumRevision) ++state.stateRevision;
                    }
                }
                for(auto& dep:peerDependencies)
                    if(dep.target==id && dep.owner==binding.sharedOwnerAccountSoid
                        && dep.targetCreated==binding.sharedOwnerCreatedRevision) dep={};
            }
        }
    }
    reap_public_leases_locked();
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return disconnected;
}
}
void disconnect(const AccountRuntimeContext& context,std::uint64_t id,
                std::uint64_t expectedJoinRevision) noexcept {
    (void)disconnect_exact(context,id,0,false,expectedJoinRevision);
}
bool disconnect_member(std::uint64_t id,std::uint64_t memberKey) noexcept {
    if(id==0 || memberKey==0) return false;
    AccountRuntimeContext selected{};
    std::size_t matches=0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    for(const auto& context:contexts) {
        const auto* row=find(context,id);
        if(row && row->joined && row->membership.hasIdentity && !row->sharedOwnerDetached
            && row->sharedOwnerAccountSoid!=0 && row->memberKey==memberKey
            && row->membership.identity.memberKey==memberKey) {
            selected=context;
            ++matches;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return matches==1 && disconnect_exact(selected,id,memberKey,true);
}
void notify_changed_locked(const ActivityState& before,ActivityState& after) noexcept {
    for (std::size_t i=0;i<after.sessions.size();++i) {
        const auto& next=after.sessions[i];
        const auto& prior=before.sessions[i];
        if (!next.occupied || !next.membership.hasIdentity) continue;
        if(prior.membership.peerConnection!=next.membership.peerConnection
            || !same_player(prior.membership.identity,next.membership.identity))
            dirty_public_sources_locked(next,after);
        if(next.sharedOwnerAccountSoid==0) continue;
        const bool changed=prior.sessionId!=next.sessionId
            || !membership::transactions::equal(prior.membership.identity,next.membership.identity)
            || !membership::transactions::equal_authoritative(prior.membership,next.membership);
        if (!changed) continue;
        if(prior.membership.peerConnection!=next.membership.peerConnection
            || !same_player(prior.membership.identity,next.membership.identity))
            dirty_peer_dependents_locked(next,&after);
        for (const auto& context:contexts) {
            if (!context.activity || context.activity==&runtime::storage::activity()) continue;
            auto& state=*context.activity;
            const auto slot=transactions::find_session(state,next.sessionId);
            if (slot==kInvalidSessionSlot || state.stateRevision==kMaximumRevision) continue;
            auto& peer=state.sessions[slot];
            if (!peer.membership.hasIdentity || peer.membership.revision==membership::kMaximumMembershipRevision
                || peer.sharedOwnerAccountSoid!=next.sharedOwnerAccountSoid
                || peer.sharedOwnerCreatedRevision!=next.sharedOwnerCreatedRevision) continue;
            ++peer.membership.revision;
            peer.membership.acknowledgedRevision=0;
            peer.recordRevision=++state.stateRevision;
        }
    }
    reap_public_leases_locked(&after);
}
bool owner_context(std::uint64_t id, AccountRuntimeContext& output) noexcept {
    output={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const bool found=root_locked(id,output)!=nullptr;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}
bool canonical_context(std::uint64_t id,AccountRuntimeContext& output) noexcept {
    output={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto& state=runtime::storage::activity();
    const auto slot=transactions::find_session(state,id);
    bool ok=false;
    if (slot!=kInvalidSessionSlot) {
        const auto& local=state.sessions[slot];
        ok=local.joined && local.ownerAccountSoid==runtime::storage::account().primarySoid
            && local.sharedOwnerAccountSoid!=0
            && root_locked(id,output,local.sharedOwnerAccountSoid,local.sharedOwnerCreatedRevision)!=nullptr;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool participant_snapshot(const SessionRecord& root,std::uint64_t account,SessionRecord& output) noexcept {
    output={};
    if (root.sharedOwnerAccountSoid==0 || root.sharedOwnerAccountSoid!=root.ownerAccountSoid) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool ok=false;
    for (const auto& context:contexts) {
        if (!context.account || context.account->primarySoid!=account) continue;
        const auto* row=find(context,root.sessionId);
        if (row && row->joined && row->membership.hasIdentity
            && row->sharedOwnerAccountSoid==root.ownerAccountSoid
            && row->sharedOwnerCreatedRevision==root.createdRevision) {
            if (ok) {ok=false;output={};break;}
            output=*row;ok=true;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool participant_accounts_snapshot(const SessionRecord& root,
                                   std::array<std::uint64_t,32>& accounts) noexcept {
    accounts={};
    if (!root.occupied || root.sessionId==0 || root.ownerAccountSoid==0
        || root.sharedOwnerAccountSoid!=root.ownerAccountSoid
        || root.sharedOwnerCreatedRevision!=root.createdRevision
        || root.createdRevision==kInvalidRevision || root.sharedMemberSlot!=0
        || root.sharedLeaseParts<2 || root.sharedLeaseParts>accounts.size()) return false;
    std::array<std::uint64_t,32> found{},keys{};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    AccountRuntimeContext owner{};
    const auto* canonical=root_locked(root.sessionId,owner,root.ownerAccountSoid,root.createdRevision);
    bool ok=canonical && canonical->sharedOwnerCreatedRevision==root.createdRevision
        && canonical->sharedMemberSlot==0 && canonical->sharedLeaseParts==root.sharedLeaseParts;
    if (ok) {
        for (const auto& context:contexts) {
            const auto* row=find(context,root.sessionId);
            if (!row || !row->joined || !row->membership.hasIdentity
                || row->sharedOwnerAccountSoid!=root.ownerAccountSoid
                || row->sharedOwnerCreatedRevision!=root.createdRevision) continue;
            const auto slot=row->sharedMemberSlot;
            const auto account=row->ownerAccountSoid;
            const auto& identity=row->membership.identity;
            if (account==0 || row->sharedOwnerDetached || row->memberKey==0
                || identity.accountSoid!=account || identity.memberKey!=row->memberKey
                || identity.opaqueSoid==0 || identity.joinIdentity==0
                || identity.joinIdentity==membership::kInvalidJoinIdentity
                || row->sharedLeaseParts!=canonical->sharedLeaseParts
                || slot>=canonical->sharedLeaseParts || slot>=found.size()
                || ((account==root.ownerAccountSoid)!=(slot==0))
                || found[slot]!=0
                || std::find(found.begin(),found.end(),account)!=found.end()
                || std::find(keys.begin(),keys.end(),row->memberKey)!=keys.end()) {
                ok=false;break;
            }
            found[slot]=account;keys[slot]=row->memberKey;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (ok) accounts=found;
    return ok;
}
bool has_joined_participants(const AccountRuntimeContext& context,std::uint64_t account,
                             std::uint64_t id) noexcept {
    if (!context.account || !context.activity || account==0 || id==0) return true;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    bool occupied=context.account->primarySoid!=account;
    const auto* root=occupied?nullptr:find(context,id);
    if (root) {
        occupied=root->joined;
        if (root->sharedOwnerAccountSoid!=0) {
            occupied|=root->sharedOwnerAccountSoid!=account
                || root->sharedOwnerCreatedRevision!=root->createdRevision;
            for (const auto& candidate:contexts) {
                const auto* peer=find(candidate,id);
                if (peer && peer->joined && peer->sharedOwnerAccountSoid==account
                    && peer->sharedOwnerCreatedRevision==root->createdRevision) occupied=true;
            }
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return occupied;
}
std::uint8_t lease_parts(const SessionRecord& record) noexcept {
    if (record.sharedLeaseParts>0) return record.sharedLeaseParts;
    std::uint8_t parts=1;
    for (std::size_t i=0;i<record.membership.reservations.size();++i)
        if (record.membership.reservations[i].memberKey!=0) parts=static_cast<std::uint8_t>(i+2);
    return parts;
}
bool private_reservations_locked(const SessionRecord& root,
    std::array<membership::Identity,31>& rows,bool fill) noexcept {
    if(root.publicWorld || root.socialWorld || root.sharedLeaseParts<kPrivateLeaseParts
        || root.sharedOwnerAccountSoid!=root.ownerAccountSoid) return true;
    std::uint32_t seen=1;
    for(const auto& context:contexts) {
        const auto* peer=find(context,root.sessionId);
        if(!peer || !peer->joined || peer->ownerAccountSoid==root.ownerAccountSoid
            || peer->sharedOwnerAccountSoid!=root.ownerAccountSoid
            || peer->sharedOwnerCreatedRevision!=root.createdRevision) continue;
        const auto slot=peer->sharedMemberSlot;
        if(slot==0 || slot>=root.sharedLeaseParts || slot>=32
            || (seen&(std::uint32_t{1}<<slot))!=0) return false;
        seen|=std::uint32_t{1}<<slot;
        const auto id=peer->membership.hasIdentity?as_reservation(peer->membership.identity):peer->membership.identity;
        if(id.memberKey!=peer->memberKey || id.accountSoid!=peer->ownerAccountSoid || id.opaqueSoid==0)return false;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& reserved=rows[i];
            if(reserved.memberKey==0)continue;
            if(i+1==slot) {
                if(reserved.memberKey!=id.memberKey || reserved.accountSoid!=id.accountSoid
                    || reserved.opaqueSoid!=id.opaqueSoid)return false;
            } else if(reserved.memberKey==id.memberKey || reserved.accountSoid==id.accountSoid)return false;
        }
        if(fill && rows[slot-1].memberKey==0)rows[slot-1]=id;
    }
    return true;
}
entity_slots::LeaseMask excluded_slots(const SessionRecord& record, std::size_t reserve) noexcept {
    using namespace entity_slots;
    const auto parts=lease_parts(record);
    const auto member=record.sharedMemberSlot;
    if (parts==1) return entity_slots::transactions::reserve_high(reserve);
    LeaseMask excluded{};
    excluded.fill(std::byte{0xFF});
    if (member>=parts || reserve>=kSlotCount) return excluded;
    const auto count=kSlotCount-reserve;
    const auto begin=count*member/parts;
    const auto end=count*(static_cast<std::size_t>(member)+1)/parts;
    for (auto bit=begin;bit<end;++bit)
        excluded[bit/8]&=~std::byte{static_cast<unsigned char>(1U<<(bit%8))};
    return excluded;
}
entity_slots::LeaseMask allocation_exclusions_locked(
    const SessionRecord& record, std::size_t reserve, bool joining) noexcept {
    if (!record.publicWorld) return joining ? excluded_slots(record,reserve)
        : entity_slots::transactions::unite(record.heldEntitySlots,excluded_slots(record,reserve));
    auto excluded=entity_slots::transactions::reserve_high(reserve);
    const auto owner=record.sharedOwnerAccountSoid;
    const auto created=record.sharedOwnerCreatedRevision;
    if (owner==0 || created==0 || record.sharedLeaseParts!=kPublicLeaseParts) {
        excluded.fill(std::byte{0xFF});return excluded;
    }
    bool canonical=false;
    for (const auto& context:contexts) {
        const auto* root=find(context,record.sessionId);
        canonical|=root && root->publicWorld && root->ownerAccountSoid==owner
            && root->createdRevision==created && root->sharedOwnerAccountSoid==owner
            && root->sharedOwnerCreatedRevision==created;
    }
    if (!canonical) {excluded.fill(std::byte{0xFF});return excluded;}
    bool capacity=false;
    for (const auto& lease:publicLeases) {
        if (lease.session==0) capacity=true;
        if (lease.session==record.sessionId && lease.owner==owner && lease.created==created) {
            excluded=entity_slots::transactions::unite(excluded,lease.outstanding);capacity=true;
        }
    }
    if (!capacity) {excluded.fill(std::byte{0xFF});return excluded;}
    for (const auto& context:contexts) {
        const auto* peer=find(context,record.sessionId);
        if (!peer || !peer->publicWorld || peer->sharedOwnerAccountSoid!=owner
            || peer->sharedOwnerCreatedRevision!=created) continue;
        excluded=entity_slots::transactions::unite(excluded,peer->heldEntitySlots);
        excluded=entity_slots::transactions::unite(excluded,peer->serverEntitySlots);
    }
    return entity_slots::transactions::unite(excluded,record.heldEntitySlots);
}
void note_slot_lease_locked(const SessionRecord& record,
    const entity_slots::LeaseMask& mask, bool returned) noexcept {
    if (!record.publicWorld || record.sharedOwnerAccountSoid==0
        || record.sharedOwnerCreatedRevision==0) return;
    PublicLeaseCensus* selected=nullptr;
    for (auto& lease:publicLeases) {
        if (lease.session==record.sessionId && lease.owner==record.sharedOwnerAccountSoid
            && lease.created==record.sharedOwnerCreatedRevision) {selected=&lease;break;}
        if (!selected && lease.session==0) selected=&lease;
    }
    if (!selected) return;
    if (selected->session==0) {
        if (returned) return;
        selected->session=record.sessionId;selected->owner=record.sharedOwnerAccountSoid;
        selected->created=record.sharedOwnerCreatedRevision;
    }
    for (std::size_t i=0;i<mask.size();++i) {
        if (returned) selected->outstanding[i]&=~mask[i];
        else selected->outstanding[i]|=mask[i];
    }
}
bool prepare_join(const AccountRuntimeContext& source,std::uint64_t id,std::uint64_t key,
                  std::uint64_t character,std::size_t requested,std::size_t reserve,
                  entity_slots::PendingMutation& output, JoinFailure* failure,
                  const membership::Identity* authorizedPrivateIdentity) noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const bool ok=prepare_locked(source,id,key,character,requested,reserve,output,failure,authorizedPrivateIdentity);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ok;
}
bool commit_join(entity_slots::PendingMutation& plan) noexcept {
    const auto prepared=plan; plan={};
    if (!prepared.prepared || prepared.peerConnection!=prepared.peerConnectionGuard || !prepared.importsShared
        || prepared.kind!=entity_slots::MutationKind::join
        || prepared.memberKey!=prepared.expectedMemberKey
        || (prepared.admitsPrivatePeer
            && !membership::transactions::equal(prepared.privateAdmission,prepared.privateAdmissionGuard))) return false;
    std::uint64_t account{};
    const auto context=capture_account_runtime(account);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    entity_slots::PendingMutation expected{};
    bool ok=context_available_locked(context)
        && prepare_locked(prepared.sharedSource,prepared.sessionId,prepared.memberKey,
            prepared.sharedCharacterSoid,prepared.requestedCount,prepared.serverReserveCount,expected,nullptr,
            prepared.admitsPrivatePeer?&prepared.privateAdmission:nullptr)
        && prepared.expectedStateRevision==expected.expectedStateRevision
        && prepared.sharedSourceRevision==expected.sharedSourceRevision
        && prepared.sharedSourceCreatedRevision==expected.sharedSourceCreatedRevision
        && prepared.sharedRecord.sharedMemberSlot==expected.sharedRecord.sharedMemberSlot
        && prepared.targetSlot==expected.targetSlot && prepared.mask==expected.mask
        && prepared.sharedRecord.destination==expected.sharedRecord.destination
        && prepared.serverMask==expected.serverMask;
    if (ok) {
        ActivityState candidate=runtime::storage::activity();
        auto& record=candidate.sessions[expected.targetSlot];
        record=expected.sharedRecord;
        record.membership.peerConnection=prepared.peerConnection;
        record.createdRevision=++candidate.stateRevision;
        record.recordRevision=candidate.stateRevision; record.joinedRevision=candidate.stateRevision;
        record.occupied=true; record.joined=true; record.memberKey=expected.memberKey;
        record.heldEntitySlots=expected.mask; record.serverEntitySlots=expected.serverMask;
        ok=(!prepared.retirement.managed
            || (prepared.retirement.activity==prepared.sessionId
                && prepared.retirement.join.member==prepared.memberKey
                && prepared.retirement.join.lease==entity_retirement::wire::from_wire_mask(expected.mask)
                && entity_retirement::can_commit_join_locked(prepared.retirement)))
            && runtime::storage::publish_activity_locked(candidate);
        if (ok) entity_retirement::commit_join_locked(prepared.retirement);
        if (ok) note_slot_lease_locked(record,expected.mask);
        if (ok) register_context_locked(context);
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
bool project(std::uint64_t id,const membership::Snapshot& self,Projection& output,
             std::uint64_t peerSourceSessionId) noexcept {
    output={};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const auto& state=runtime::storage::activity();
    const auto slot=transactions::find_session(state,id);
    AccountRuntimeContext owner{};
    const auto* root=slot==kInvalidSessionSlot?nullptr:root_locked(id,owner,
        state.sessions[slot].sharedOwnerAccountSoid,state.sessions[slot].sharedOwnerCreatedRevision);
    bool ok=slot!=kInvalidSessionSlot && root;
    if (ok) {
        const auto& local=state.sessions[slot];
        ok=local.ownerAccountSoid==runtime::storage::account().primarySoid
            && local.sharedOwnerAccountSoid==root->ownerAccountSoid
            && local.sharedOwnerCreatedRevision==root->createdRevision && local.sharedMemberSlot<32;
        if (ok) {
            output.localSlot=local.sharedMemberSlot;
            output.members[0]=root->membership.identity;
            output.peerConnections[0]=root->membership.peerConnection;
            if (root->membership.hasIdentity) output.identityWireWordMask=1;
            if (root->joined && root->membership.hasIdentity) output.joinedMemberMask=1;
            // type12 is staged before its reservation commits, so use its pending after-image here;
            // a participant's copied table is not canonical
            const auto& reservations=&local==root?self.reservations:root->membership.reservations;
            for (std::size_t i=0;i<31;++i) output.members[i+1]=reservations[i];
            for (const auto& context:contexts) {
                const auto* peer=find(context,id);
                if (!peer || !peer->joined || !peer->membership.hasIdentity
                    || peer->sharedOwnerAccountSoid!=root->ownerAccountSoid
                    || peer->sharedOwnerCreatedRevision!=root->createdRevision
                    || peer->sharedMemberSlot>=32) continue;
                output.members[peer->sharedMemberSlot]=peer->membership.identity;
                output.peerConnections[peer->sharedMemberSlot]=peer->membership.peerConnection;
                output.identityWireWordMask|=std::uint32_t{1}<<peer->sharedMemberSlot;
                output.joinedMemberMask|=std::uint32_t{1}<<peer->sharedMemberSlot;
                output.tokens[peer->sharedMemberSlot]=peer->membership.transitionToken;
            }
            output.members[output.localSlot]=self.identity;
            output.peerConnections[output.localSlot]=self.peerConnection;
            output.identityWireWordMask|=std::uint32_t{1}<<output.localSlot;
            if (local.joined) output.joinedMemberMask|=std::uint32_t{1}<<output.localSlot;
            output.tokens[output.localSlot]=self.transitionToken;
            output.hostKey=root->membership.identity.memberKey;
            output.spawn=output.localSlot==0?self.spawn:root->membership.spawn;
            output.teleport=output.localSlot==0?self.teleport:root->membership.teleport;
            if(root->sharedOwnerDetached) {
                output.hostKey=0;
                for(std::size_t i=0;i<32;++i) {
                    if((output.joinedMemberMask&(std::uint32_t{1}<<i))==0) continue;
                    output.hostKey=output.members[i].memberKey;
                    for(const auto& context:contexts) {
                        const auto* peer=find(context,id);
                        if(!peer || !peer->joined || peer->sharedMemberSlot!=i
                            || peer->sharedOwnerAccountSoid!=root->ownerAccountSoid
                            || peer->sharedOwnerCreatedRevision!=root->createdRevision) continue;
                        output.spawn=i==output.localSlot?self.spawn:peer->membership.spawn;
                        output.teleport=i==output.localSlot?self.teleport:peer->membership.teleport;
                        break;
                    }
                    break;
                }
            }
            if(peerSourceSessionId!=0) map_peer_transport_locked(peerSourceSessionId,*root,output);
            if(root->socialWorld) {output.spawn=self.spawn;output.teleport=self.teleport;}
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
}

namespace dawn::state::matchmaking {
bool social_world_locked(std::uint64_t account,activity::SessionRecord& output) noexcept {
    output={};bool found=false;
    for(const auto& context:activity::shared::contexts) {
        if(!context.account || !context.activity || context.account->primarySoid!=account)continue;
        for(const auto& row:context.activity->sessions) {
            if(!row.occupied || !row.socialWorld || !row.joined || row.restoredDormant
                || row.sharedOwnerDetached || !row.membership.hasIdentity
                || row.ownerAccountSoid!=account || row.membership.identity.accountSoid!=account)continue;
            AccountRuntimeContext owner{};
            const auto* root=activity::shared::root_locked(row.sessionId,owner,
                row.sharedOwnerAccountSoid,row.sharedOwnerCreatedRevision);
            if(!root || !activity::shared::social_root(*root))continue;
            if(found){output={};return false;}
            output=row;found=true;
        }
    }
    return found;
}
bool social_world_matches_locked(std::uint64_t account,std::uint64_t session,std::uint64_t generation,
    std::uint32_t activityHash) noexcept {
    activity::SessionRecord world{};
    return social_world_locked(account,world) && world.sessionId==session
        && world.sharedOwnerCreatedRevision==generation
        && world.destination.packageNameLength<=world.destination.packageName.size()
        && middleware::content::social_activity_policy::matches(activityHash,
            world.destination.activityIndex,
            {reinterpret_cast<const char*>(world.destination.packageName.data()),
             world.destination.packageNameLength});
}
}
