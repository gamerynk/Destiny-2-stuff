#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../../middleware/bap/activity_message/entity_retirement_wire.h"
#include "../../../middleware/bap/activity_message/request_provenance.h"

namespace dawn::state::activity::entity_retirement {

namespace wire = middleware::bap::activity_message::entity_retirement;
using Mask=wire::Mask;
using RequestProvenance=middleware::bap::activity_message::RequestProvenance;

template<std::size_t JournalCapacity=128, std::size_t ParticipantCapacity=32>
struct Journal {
    static_assert(JournalCapacity>0 && ParticipantCapacity>0);
    struct Participant {
        std::uint64_t account{},member{},cursor{},joinRevision{};
        Mask lease{},reported{};
        bool joined{},departed{};
        std::uint64_t requestConnection{},requestOrdinal{};
        std::uint32_t requestTask{};
        std::uint64_t nativeBaselineSerial{};
    };
    struct Operation {
        std::uint64_t serial{},account{},member{},joinRevision{};
        Mask mask{};
        std::uint8_t selector{};
        std::int8_t reason{};
        RequestProvenance request{};
        Mask originalRequestMask{};
    };
    struct Join {
        std::uint64_t account{},member{},before{},through{},revision{};
        Mask lease{};
        std::size_t slot{ParticipantCapacity};
        bool valid{};
        bool operator==(const Join&) const = default;
    };
    enum class Result { queued, duplicate, refused, capacity };
    std::array<Participant,ParticipantCapacity> participants{};
    std::array<Operation,JournalCapacity> operations{};
    std::uint64_t serial{},revision{},ownershipRevision{};

    Participant* find(std::uint64_t account) noexcept {
        if (account==0) return nullptr;
        for (auto& p:participants) if (p.account==account) return &p;
        return nullptr;
    }
    const Participant* find(std::uint64_t account) const noexcept {
        if (account==0) return nullptr;
        for (const auto& p:participants) if (p.account==account) return &p;
        return nullptr;
    }
    const Operation* at(std::uint64_t wanted) const noexcept {
        if (wanted==0 || wanted>serial) return nullptr;
        const auto& op=operations[static_cast<std::size_t>((wanted-1)%JournalCapacity)];
        return op.serial==wanted ? &op : nullptr;
    }
    Join prepare_join(std::uint64_t account,std::uint64_t member,const Mask& lease) const noexcept {
        Join out{};
        if (account==0 || member==0 || wire::empty(lease)
            || revision==(std::numeric_limits<std::uint64_t>::max)()) return out;
        std::size_t free=ParticipantCapacity;
        for (std::size_t i=0;i<participants.size();++i) {
            if (participants[i].account==account) { free=i; break; }
            if (participants[i].account==0 && free==ParticipantCapacity) free=i;
        }
        if (free==ParticipantCapacity) return out;
        const auto& p=participants[free];
        for (const auto& other:participants) {
            if (!other.joined || other.account==account) continue;
            for (std::size_t i=0;i<lease.size();++i)
                if ((lease[i]&other.lease[i])!=0) return out;
        }
        // a returning native view starts at its last delivered epoch and consumes the retained
        // journal after join-result and before leases/membership permit new entities
        const auto before=p.account==account ? p.cursor : serial;
        for (auto n=before;n<serial;) if (at(++n)==nullptr) return out;
        out={account,member,before,serial,revision,lease,free,true};
        return out;
    }
    bool can_commit_join(const Join& join) const noexcept {
        return join.valid && join==prepare_join(join.account,join.member,join.lease);
    }
    bool commit_join(const Join& join) noexcept {
        if (!can_commit_join(join)) return false;
        auto& p=participants[join.slot];
        if (p.account!=0 && p.account!=join.account) return false;
        p={join.account,join.member,join.through,revision+1,join.lease,{},true,false};
        p.nativeBaselineSerial=join.through;
        ++revision;
        ++ownershipRevision;
        return true;
    }
    struct LeaseChange {
        std::uint64_t account{},member{},joinRevision{},ownershipRevision{};
        bool valid{};
        bool operator==(const LeaseChange&) const = default;
    };
    LeaseChange prepare_lease_change(std::uint64_t account,std::uint64_t member,
        const Mask& before,const Mask& after) const noexcept {
        const auto* p=find(account);
        if (!p || !p->joined || p->departed || p->member!=member || p->lease!=before
            || revision==(std::numeric_limits<std::uint64_t>::max)()
            || ownershipRevision==(std::numeric_limits<std::uint64_t>::max)()) return {};
        for (const auto& other:participants) if (other.joined && other.account!=account)
            for (std::size_t i=0;i<after.size();++i) if ((after[i]&other.lease[i])!=0) return {};
        return {account,member,p->joinRevision,ownershipRevision,true};
    }
    bool can_commit_lease_change(const LeaseChange& change,const Mask& before,const Mask& after) const noexcept {
        return change.valid && change==prepare_lease_change(change.account,change.member,before,after);
    }
    bool commit_lease_change(const LeaseChange& change,const Mask& before,const Mask& after) noexcept {
        if (!can_commit_lease_change(change,before,after)) return false;
        auto* p=find(change.account);
        if (before==after) return true;
        p->lease=after;
        ++revision;++ownershipRevision;
        return true;
    }
    bool depart(std::uint64_t account,std::uint64_t member,const Mask& lease) noexcept {
        auto* p=find(account);
        if (!p || !p->joined || p->member!=member || !wire::subset(lease,p->lease)
            || revision==(std::numeric_limits<std::uint64_t>::max)()) return false;
        p->lease=lease;
        p->joined=false;
        p->departed=true;
        ++revision;
        ++ownershipRevision;
        return true;
    }
    Result abandon(std::uint64_t account,const wire::Abandon& release) noexcept {
        auto* p=find(account);
        if (!p || !p->departed || p->joined || release.reason!=3 || release.selector>64
            || wire::empty(release.mask) || !wire::subset(release.mask,p->lease)) return Result::refused;
        for (const auto& current:participants) if (current.joined)
            for (std::size_t i=0;i<release.mask.size();++i)
                if ((release.mask[i]&current.lease[i])!=0) return Result::refused;
        return queue_abandon(*p,release);
    }
    // a native unloaded cell can abandon local facets while the session stays joined. Never clear
    // reported bits just because a purge was delivered: message 26 has no salt, native 25 no fence
    Result abandon_current(std::uint64_t account,std::uint64_t joinRevision,
                           const wire::Abandon& release,const RequestProvenance& request={},
                           const Mask& delegatedBirths={}) noexcept {
        auto* p=find(account);
        if(!p || !p->joined || p->departed || !joinRevision || p->joinRevision!=joinRevision
            || release.reason!=3 || release.selector>64 || wire::empty(release.mask)) return Result::refused;
        Mask permitted=p->lease;
        for(std::size_t i=0;i<permitted.size();++i)permitted[i]|=delegatedBirths[i];
        if(!wire::subset(release.mask,permitted))return Result::refused;
        if(!request.valid() || (p->requestConnection && p->requestConnection!=request.connectionIncarnation)
            || request.authenticatedOrdinal<=p->requestOrdinal)return Result::refused;
        // native 174004D/174019E issues a u32 wire task once per retained request; reject retries
        // and delayed tasks (unsigned subtraction admits a bounded wrap)
        const auto taskDistance=static_cast<std::uint32_t>(request.taskId-p->requestTask);
        if(p->requestConnection && (!taskDistance || taskDistance>=0x80000000U))return Result::refused;
        for(const auto& current:participants) if(current.joined && current.account!=account)
            for(std::size_t i=0;i<release.mask.size();++i)
                if((release.mask[i]&current.lease[i])!=0) return Result::refused;
        const auto result=queue_abandon(*p,release);
        if(result==Result::queued || result==Result::duplicate) {
            p->requestConnection=request.connectionIncarnation;p->requestOrdinal=request.authenticatedOrdinal;p->requestTask=request.taskId;
            if(result==Result::queued)operations[static_cast<std::size_t>((serial-1)%JournalCapacity)].request=request;
        }
        return result;
    }
private:
    Result queue_abandon(Participant& participant,const wire::Abandon& release) noexcept {
        auto* p=&participant;
        Mask delta{};
        for (std::size_t i=0;i<delta.size();++i) delta[i]=release.mask[i]&~p->reported[i];
        if (wire::empty(delta)) return Result::duplicate;
        if (serial==(std::numeric_limits<std::uint64_t>::max)()
            || revision==(std::numeric_limits<std::uint64_t>::max)()) return Result::capacity;
        auto& target=operations[static_cast<std::size_t>(serial%JournalCapacity)];
        for (const auto& recipient:participants)
            if (recipient.account!=0 && target.serial!=0 && recipient.cursor<target.serial)
                return Result::capacity;
        const auto next=serial+1;
        target={next,p->account,p->member,p->joinRevision,delta,release.selector,release.reason};
        target.originalRequestMask=release.mask;
        for (std::size_t i=0;i<delta.size();++i) p->reported[i]|=delta[i];
        serial=next;
        ++revision;
        return Result::queued;
    }
public:
    Result queue_server_purge(std::uint64_t expectedRevision,std::uint64_t expectedSerial,
        std::uint64_t account,std::uint64_t member,std::uint64_t joinRevision,
        const Mask& mask,const Mask& serverReserved) noexcept {
        const auto* p=find(account);
        if(revision!=expectedRevision||serial!=expectedSerial||!p||!p->joined||p->departed
            ||p->member!=member||p->joinRevision!=joinRevision||wire::empty(mask)
            ||!wire::subset(mask,serverReserved))return Result::refused;
        for(const auto& other:participants)if(other.joined)
            for(std::size_t i=0;i<mask.size();++i)if(mask[i]&other.lease[i])return Result::refused;
        if(serial==UINT64_MAX||revision==UINT64_MAX)return Result::capacity;
        auto& target=operations[static_cast<std::size_t>(serial%JournalCapacity)];
        for(const auto& recipient:participants)
            if(recipient.account&&target.serial&&recipient.cursor<target.serial)return Result::capacity;
        target={serial+1,account,member,joinRevision,mask,0,0};++serial;++revision;return Result::queued;
    }
    bool delivered(std::uint64_t account,std::uint64_t expectedBefore,std::uint64_t through) noexcept {
        auto* p=find(account);
        if (!p || p->cursor!=expectedBefore || through<=expectedBefore || through>serial
            || revision==(std::numeric_limits<std::uint64_t>::max)()) return false;
        for (auto n=expectedBefore;n<through;) if (at(++n)==nullptr) return false;
        p->cursor=through;
        ++revision;
        return true;
    }
};

} // namespace dawn::state::activity::entity_retirement
