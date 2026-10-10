#include <cstdio>
#include <cstring>
#include "state/activity/shared/runtime.h"
#include "state/activity/shared/public_admission.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"
#include "state/activity/entity_slots/transactions/internal.h"
using namespace dawn::state;
namespace dawn::state {
namespace { const AccountRuntimeContext* selected{}; }
AccountRuntimeContext capture_account_runtime(std::uint64_t& id) noexcept {
    id=selected->account->primarySoid;return *selected;
}
AccountState account_snapshot() noexcept {return *selected->account;}
ScopedAccountRuntime::ScopedAccountRuntime(const AccountRuntimeContext& c) noexcept : previous_(selected) {selected=&c;}
ScopedAccountRuntime::~ScopedAccountRuntime(){selected=previous_;}
namespace runtime::storage {
SRWLOCK g_stateLock=SRWLOCK_INIT;
AccountState& account() noexcept{return *selected->account;}
activity::ActivityState& activity() noexcept{return *selected->activity;}
bool publish_activity_locked(const dawn::state::activity::ActivityState& next) noexcept {
    dawn::state::activity::shared::notify_changed_locked(activity(),const_cast<dawn::state::activity::ActivityState&>(next));
    activity()=next;return true;
}
}
namespace activity::destination {bool valid(const DestinationSelection& d) noexcept{return d.packageNameLength<=d.packageName.size();}}
}
bool lease_incarnation_case() {
    activity::entity_retirement::History history{};
    activity::entity_retirement::wire::Mask first{},extra{},changed{};
    first[0]=1;extra[0]=2;changed[0]=3;
    const auto join=history.prepare_join(7,9,first);
    if(!history.commit_join(join))return false;
    const auto lease=history.prepare_lease_change(7,9,first,changed);
    if(!lease.valid || !history.commit_lease_change(lease,first,changed)
        || !history.depart(7,9,changed))return false;
    const auto next=history.prepare_join(7,9,first);
    if(!history.commit_join(next) || history.can_commit_lease_change(lease,first,changed))return false;
    if(history.prepare_lease_change(7,10,first,changed).valid
        || history.prepare_lease_change(7,9,extra,changed).valid)return false;
    const auto noOp=history.prepare_lease_change(7,9,first,first);
    const auto revision=history.ownershipRevision;
    if(!history.commit_lease_change(noOp,first,first) || history.ownershipRevision!=revision)return false;
    std::puts("PASS: journal lease incarnation/member/previous-mask guards and zero-grant no-op stability");
    return true;
}
bool real_join_case() {
    AccountState accounts[2]{};activity::ActivityState worlds[2]{};
    AccountRuntimeContext context[2]{{&accounts[0],&worlds[0]},{&accounts[1],&worlds[1]}};
    for(unsigned i=0;i<2;++i) {
        accounts[i].primarySoid=300+i;worlds[i].stateRevision=30;
        auto& source=worlds[i].sessions[1];source.occupied=source.joined=true;
        source.sessionId=1100+i;source.ownerAccountSoid=300+i;
        source.createdRevision=source.recordRevision=30;source.memberKey=700+i;
        source.membership.hasIdentity=true;auto& id=source.membership.identity;
        id.accountSoid=300+i;id.memberKey=700+i;id.opaqueSoid=800+i;id.joinIdentity=900+i;
        source.destination.activityIndex=40;constexpr char name[]="edz_freeroam";
        source.destination.packageNameLength=sizeof(name)-1;
        std::memcpy(source.destination.packageName.data(),name,sizeof(name)-1);
        activity::shared::register_context_locked(context[i]);
    }
    auto& root=worlds[0].sessions[0];root=worlds[0].sessions[1];root.sessionId=1000;
    root.joined=false;root.publicWorld=true;root.memberKey=0;
    root.sharedOwnerAccountSoid=300;root.sharedOwnerCreatedRevision=root.createdRevision;
    root.sharedLeaseParts=32;root.sharedMemberSlot=0;
    if(!activity::shared::admit_public_source(context[0],1000,context[0],1100)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    const auto ownerJoin=[&](){ScopedAccountRuntime scope(context[0]);activity::entity_slots::PendingMutation p{};
        return activity::entity_slots::prepare_join(1000,700,600,256,p)
            && activity::entity_retirement::prepare_join(1000,300,700,p.mask,p.retirement)
            && activity::entity_slots::commit(p);};
    if(!ownerJoin()){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}const auto first=root.heldEntitySlots;
    if(!ownerJoin()){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}const auto repeated=root.heldEntitySlots;
    for(std::size_t i=0;i<first.size();++i)if((first[i]&repeated[i])!=std::byte{}){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    if(!activity::shared::admit_public_source(context[0],1000,context[1],1101)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    std::uint64_t guestJoinRevision=0;
    {ScopedAccountRuntime scope(context[1]);activity::entity_slots::PendingMutation p{};
        if(!activity::shared::prepare_join(context[0],1000,701,801,600,256,p)
            || !activity::entity_retirement::prepare_join(1000,301,701,p.mask,p.retirement)
            || !(guestJoinRevision=p.retirement.join.revision+1)
            || !activity::entity_slots::commit(p)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    }
    worlds[1].sessions[0].membership.hasIdentity=true;
    const auto guest=worlds[1].sessions[0].heldEntitySlots;
    for(std::size_t i=0;i<guest.size();++i)if(((first[i]|repeated[i])&guest[i])!=std::byte{}){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    const auto ownerReturned=root.heldEntitySlots;
    {ScopedAccountRuntime scope(context[0]);activity::entity_slots::PendingMutation p{};
        if(!activity::entity_slots::prepare_release(1000,ownerReturned,p)
            || !activity::entity_slots::commit(p)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    }
    {ScopedAccountRuntime scope(context[1]);activity::entity_slots::PendingMutation p{};
        if(!activity::entity_slots::prepare_grant(1000,100,p)
            || !activity::entity_slots::commit(p)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    }
    if(!activity::shared::disconnect_member(1000,701)
        || !activity::entity_retirement::close_view(1000,301,guestJoinRevision)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    {ScopedAccountRuntime scope(context[1]);activity::entity_slots::PendingMutation p{};
        if(!activity::shared::prepare_join(context[0],1000,701,801,600,256,p)
            || !activity::entity_retirement::prepare_join(1000,301,701,p.mask,p.retirement)
            || !activity::entity_slots::commit(p)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    }
    worlds[1].sessions[0].membership.hasIdentity=true;
    if(!activity::shared::disconnect_member(1000,700) || !root.sharedOwnerDetached || !ownerJoin()){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    for(std::size_t i=0;i<guest.size();++i)
        if(((first[i]|repeated[i]|guest[i])&root.heldEntitySlots[i])!=std::byte{}){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    {ScopedAccountRuntime scope(context[0]);activity::entity_slots::PendingMutation p{},join{};
        if(!activity::entity_slots::prepare_grant(1000,8192,p) || !activity::entity_slots::commit(p)
            || activity::entity_slots::prepare_join(1000,700,600,256,join)){std::printf("FAIL: managed join fixture line %d\n",__LINE__);return false;}
    }
    activity::shared::reset_locked();
    std::puts("PASS: real managed joins/grants/returns, late borrowed IDs qualify departure, repeated joins and exhaustion refusal");
    return true;
}
int main() {
    if(!lease_incarnation_case()){std::puts("FAIL: lease incarnation guards");return 1;}
    if(!real_join_case()){std::puts("FAIL: real public join transaction fixture");return 1;}
    AccountState accounts[2]{};activity::ActivityState worlds[2]{};
    for(unsigned i=0;i<2;++i) {
        accounts[i].primarySoid=100+i;worlds[i].stateRevision=20;
        auto& r=worlds[i].sessions[0];r.occupied=true;r.joined=true;r.publicWorld=true;
        r.sessionId=500;r.ownerAccountSoid=100+i;r.sharedOwnerAccountSoid=100;
        r.createdRevision=10+i;r.recordRevision=20;r.sharedOwnerCreatedRevision=10;r.sharedLeaseParts=32;
        r.sharedMemberSlot=static_cast<std::uint8_t>(i);r.memberKey=600+i;
        r.serverEntitySlots=activity::entity_slots::transactions::reserve_high(256);
        activity::shared::register_context_locked({&accounts[i],&worlds[i]});
    }
    const auto check=[](bool value,const char* text){if(!value)std::printf("FAIL: %s\n",text);return value;};
    auto& one=worlds[0].sessions[0];auto& two=worlds[1].sessions[0];
    AccountRuntimeContext contextOne{&accounts[0],&worlds[0]},contextTwo{&accounts[1],&worlds[1]};
    activity::entity_slots::PendingMutation first{},second{};
    {ScopedAccountRuntime scope(contextOne);if(!activity::entity_slots::prepare_grant(500,600,first))return 1;}
    {ScopedAccountRuntime scope(contextTwo);if(!activity::entity_slots::prepare_grant(500,600,second))return 1;}
    {ScopedAccountRuntime scope(contextOne);if(!check(activity::entity_slots::commit(first),"real first grant commit"))return 1;}
    {ScopedAccountRuntime scope(contextTwo);
        if(!check(!activity::entity_slots::commit(second),"real cross-account stale grant rejected"))return 1;
    }
    {ScopedAccountRuntime scope(contextOne);activity::entity_slots::PendingMutation release{};
        if(!activity::entity_slots::prepare_release(500,one.heldEntitySlots,release)
            || !activity::entity_slots::commit(release))return 1;
    }
    auto proposed=activity::entity_slots::transactions::select_free(activity::shared::allocation_exclusions_locked(one,256,true),600);
    auto competing=activity::entity_slots::transactions::select_free(activity::shared::allocation_exclusions_locked(two,256,true),600);
    if(!check(proposed==competing,"simultaneous prepares observe same free range"))return 1;
    one.heldEntitySlots=proposed;activity::shared::note_slot_lease_locked(one,proposed);
    auto next=activity::entity_slots::transactions::select_free(activity::shared::allocation_exclusions_locked(two,256,true),600);
    if(!check(next!=competing,"commit recomputation observes other account issuance"))return 1;
    two.heldEntitySlots=next;activity::shared::note_slot_lease_locked(two,next);
    for(std::size_t i=0;i<next.size();++i)if(!check((next[i]&proposed[i])==std::byte{},"disjoint elastic ranges"))return 1;
    one.heldEntitySlots={};one.joined=false;
    auto afterDeparture=activity::shared::allocation_exclusions_locked(two,256,false);
    for(std::size_t i=0;i<proposed.size();++i)if(!check((afterDeparture[i]&proposed[i])==proposed[i],"departure retains old native IDs"))return 1;
    activity::shared::note_slot_lease_locked(one,proposed,true);
    auto regrant=activity::entity_slots::transactions::select_free(activity::shared::allocation_exclusions_locked(two,256,false),600);
    if(!check(regrant==proposed,"explicit return permits stable exact reuse"))return 1;
    activity::shared::note_slot_lease_locked(two,regrant);two.heldEntitySlots=activity::entity_slots::transactions::unite(next,regrant);
    auto& late=worlds[0].sessions[1];late=one;late.ownerAccountSoid=100;late.memberKey=900;late.heldEntitySlots={};
    auto lateGrant=activity::entity_slots::transactions::select_free(activity::shared::allocation_exclusions_locked(late,256,true),600);
    for(std::size_t i=0;i<lateGrant.size();++i)if(!check((lateGrant[i]&two.heldEntitySlots[i])==std::byte{},"late join cannot regrant borrowed home IDs"))return 1;
    const auto old=one.sharedOwnerCreatedRevision;one.sharedOwnerCreatedRevision=0;
    if(!check(activity::entity_slots::transactions::count(activity::shared::allocation_exclusions_locked(one,256,true))==8192,"invalid incarnation fails closed"))return 1;
    one.sharedOwnerCreatedRevision=old;
    const auto oldRecord=one;
    worlds[1].sessions[0]={};
    {ScopedAccountRuntime scope(contextOne);auto empty=worlds[0];empty.sessions={};
        if(!runtime::storage::publish_activity_locked(empty))return 1;
    }
    worlds[0].sessions[0]=oldRecord;worlds[0].sessions[0].heldEntitySlots={};
    const auto reopened=activity::shared::allocation_exclusions_locked(worlds[0].sessions[0],256,true);
    if(!check(activity::entity_slots::transactions::count(reopened)==256,"complete incarnation teardown releases census capacity"))return 1;

    std::puts("PASS: real shared allocator contexts; elastic disjoint leases, late arrivals, retained departures, explicit returns and invalid identity");
}
