#include "runtime.h"
#include "../../runtime/storage/internal.h"
#include <cassert>
#include "../../../middleware/bap/activity_message/departure_query_wire.h"

namespace dawn::state::activity::entity_retirement {
namespace {
struct Room { std::uint64_t activity{}; History history{}; Queries queries{}; };
std::array<Room,256> rooms{};
Room* find(std::uint64_t activity) noexcept {
    if (!activity) return nullptr;
    for (auto& room:rooms) if (room.activity==activity) return &room;
    return nullptr;
}
}
bool prepare_lease_change_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
    const wire::Mask& before,const wire::Mask& after,PendingLeaseChange& out) noexcept {
    out={};out.activity=activity;
    const auto* room=find(activity);
    if (!room) return true;
    out.managed=true;out.change=room->history.prepare_lease_change(account,member,before,after);
    return out.change.valid;
}
bool can_commit_lease_change_locked(const PendingLeaseChange& plan,
    const wire::Mask& before,const wire::Mask& after) noexcept {
    const auto* room=find(plan.activity);
    if (!plan.managed) return room==nullptr;
    return room && plan.change.account==runtime::storage::account().primarySoid
        && room->history.can_commit_lease_change(plan.change,before,after);
}
void commit_lease_change_locked(const PendingLeaseChange& plan,
    const wire::Mask& before,const wire::Mask& after) noexcept {
    if (!plan.managed) return;
    auto* room=find(plan.activity);
    const bool ok=room && room->history.commit_lease_change(plan.change,before,after);
    assert(ok);(void)ok;
}
bool prepare_join(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
                  std::span<const std::byte,1024> lease,PendingJoin& output) noexcept {
    output={};
    if (!activity || !account || !member) return false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    if (!room) for (auto& candidate:rooms) if (!candidate.activity) {
        candidate.activity=activity; room=&candidate; break;
    }
    if (room) output={activity,room->history.prepare_join(account,member,wire::from_wire_mask(lease)),true};
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return output.managed && output.join.valid;
}
bool can_commit_join_locked(const PendingJoin& plan) noexcept {
    if (!plan.managed) return true;
    const auto* room=find(plan.activity);
    return room && plan.join.account==runtime::storage::account().primarySoid
        && room->history.can_commit_join(plan.join);
}
void commit_join_locked(const PendingJoin& plan) noexcept {
    if (!plan.managed) return;
    auto* room=find(plan.activity);
    const bool ok=room && room->history.commit_join(plan.join);
    assert(ok); (void)ok;
}
void depart_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
                    std::span<const std::byte,1024> lease) noexcept {
    if (auto* room=find(activity)) (void)room->history.depart(account,member,wire::from_wire_mask(lease));
}
void reset_locked() noexcept {
    for (auto& room:rooms) room={};
}
bool matches_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision) noexcept {
    const auto* room=find(activity);
    const auto* p=room?room->history.find(account):nullptr;
    return p && joinRevision!=0 && p->joinRevision==joinRevision;
}
bool close_view(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    const bool ok=room && room->queries.closed(room->history,account,joinRevision);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
bool next_query(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
                std::uint64_t now,Queries::Plan& out) noexcept {
    out={};
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    if (auto* room=find(activity)) out=room->queries.prepare(room->history,account,joinRevision,now);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return out.valid;
}
bool publish_query(std::uint64_t activity,const Queries::Plan& plan,std::uint64_t now,
                   void (*copy)(void*) noexcept,void* context) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    const bool ok=room && copy && room->queries.can_publish(room->history,plan,now);
    if (ok) {copy(context);room->queries.published(plan,now);}
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
Queries::Reply answer_query(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
                            unsigned type,std::span<const std::byte> body,std::uint64_t now) noexcept {
    wire::QueryReply reply{};
    if (!wire::parse_query_reply(type,body,reply)) return Queries::Reply::refused;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    const auto result=room?room->queries.answer(room->history,account,joinRevision,reply.correlation,
        type,reply.selector,reply.mask,now):Queries::Reply::refused;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return result;
}
History::Result abandon(std::uint64_t activity,std::uint64_t account,
                         const wire::Abandon& release) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    const auto result=room?room->history.abandon(account,release):History::Result::refused;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return result;
}
bool join_operation(const PendingJoin& plan,std::uint64_t serial,History::Operation& out) noexcept {
    out={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto* room=find(plan.activity);
    const auto* op=room && plan.managed && serial>plan.join.before && serial<=plan.join.through
        && room->history.can_commit_join(plan.join)?room->history.at(serial):nullptr;
    if (op) out=*op;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return out.serial!=0;
}
bool next(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
           History::Operation& out) noexcept {
    out={};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto* room=find(activity);
    const auto* participant=room?room->history.find(account):nullptr;
    const auto* op=participant && joinRevision!=0 && participant->joinRevision==joinRevision
        && participant->cursor<room->history.serial?room->history.at(participant->cursor+1):nullptr;
    if (op) out=*op;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return out.serial!=0;
}
bool publish(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
              std::uint64_t serial,void (*copy)(void*) noexcept,void* context) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    auto* room=find(activity);
    const auto* p=room?room->history.find(account):nullptr;
    const bool ok=copy && serial!=0 && p && joinRevision!=0 && p->joinRevision==joinRevision
        && p->cursor==serial-1 && room->history.at(serial)
        && room->history.revision!=(std::numeric_limits<std::uint64_t>::max)();
    if (ok) {
        copy(context);
        const bool committed=room->history.delivered(account,serial-1,serial);
        assert(committed); (void)committed;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return ok;
}
}
