#pragma once

#include "policy.h"

namespace dawn::state::activity::entity_retirement {

template<class Ledger> struct DepartureQueries {
    static constexpr std::uint64_t kTimeoutMs=15000;
    static constexpr unsigned kMaximumAttempts=3;
    static constexpr auto kCapacity=std::tuple_size_v<decltype(Ledger::participants)>;
    struct Job {
        std::uint64_t account{},joinRevision{};
        unsigned attempts{};
        bool complete{};
    };
    struct Plan {
        std::uint64_t subject{},subjectJoin{},recipient{},recipientJoin{},ownershipRevision{};
        std::uint32_t correlation{};
        Mask lease{};
        std::size_t slot{kCapacity};
        bool valid{};
        bool operator==(const Plan&) const = default;
    };
    struct Flight {
        Plan plan{};
        Mask live{};
        std::array<bool,65> seen{};
        std::uint64_t due{};
        bool active{};
    };
    enum class Reply { refused, accepted, completed, queued, capacity };
    std::array<Job,kCapacity> jobs{};
    Flight flight{};
    std::uint32_t sequence{};

    bool closed(const Ledger& history,std::uint64_t account,std::uint64_t join) noexcept {
        const auto* p=history.find(account);
        if (!p || !join || p->joinRevision!=join || p->joined || !p->departed) return false;
        for (std::size_t i=0;i<history.participants.size();++i) if (&history.participants[i]==p) {
            auto& job=jobs[i];
            if (job.account==account && job.joinRevision==join) return true;
            job={account,join,0,false};
            return true;
        }
        return false;
    }
    bool current(const Ledger& history,const Plan& plan) const noexcept {
        const auto* subject=history.find(plan.subject);
        const auto* recipient=history.find(plan.recipient);
        return plan.valid && plan.slot<jobs.size() && subject && recipient
            && plan.subject!=plan.recipient && !subject->joined && subject->departed
            && subject->joinRevision==plan.subjectJoin && subject->lease==plan.lease
            && recipient->joined && recipient->joinRevision==plan.recipientJoin
            && history.ownershipRevision==plan.ownershipRevision
            && jobs[plan.slot].account==plan.subject && jobs[plan.slot].joinRevision==plan.subjectJoin
            && !jobs[plan.slot].complete;
    }
    Plan prepare(const Ledger& history,std::uint64_t account,std::uint64_t join,
                 std::uint64_t now) noexcept {
        if (flight.active && (now>=flight.due || !current(history,flight.plan))) flight={};
        Plan plan{};
        const auto* recipient=history.find(account);
        if (flight.active || !recipient || !join || recipient->joinRevision!=join
            || !recipient->joined || recipient->cursor!=history.serial || sequence>=0x7FFFFFFFU
            || now>(std::numeric_limits<std::uint64_t>::max)()-kTimeoutMs) return plan;
        for (std::size_t i=0;i<jobs.size();++i) {
            auto& job=jobs[i];
            const auto* subject=history.find(job.account);
            if (!subject || job.complete || job.attempts>=kMaximumAttempts) continue;
            if (subject->joined || !subject->departed || subject->joinRevision!=job.joinRevision) {
                job.complete=true; continue;
            }
            if (subject->account==account) continue;
            plan={subject->account,subject->joinRevision,account,join,history.ownershipRevision,
                  0x80000000U|(sequence+1),subject->lease,i,true};
            return plan;
        }
        return plan;
    }
    bool can_publish(const Ledger& history,const Plan& plan,std::uint64_t now) noexcept {
        return plan.valid && plan==prepare(history,plan.recipient,plan.recipientJoin,now);
    }
    void published(const Plan& plan,std::uint64_t now) noexcept {
        ++sequence;
        ++jobs[plan.slot].attempts;
        flight={}; flight.plan=plan; flight.due=now+kTimeoutMs; flight.active=true;
    }
    Reply answer(Ledger& history,std::uint64_t account,std::uint64_t join,std::uint32_t correlation,
                 unsigned type,unsigned selector,const Mask& mask,std::uint64_t now) noexcept {
        if (!flight.active || account!=flight.plan.recipient || join!=flight.plan.recipientJoin
            || correlation!=flight.plan.correlation) return Reply::refused;
        if (now>=flight.due || !current(history,flight.plan)) {flight={};return Reply::refused;}
        if (type==31) {
            // the native worker emits exactly one nonempty bucket per selector; contradictory buckets
            // are no safe basis for deletion, so nothing mutates until the terminal 32
            if (selector>64 || flight.seen[selector] || wire::empty(mask)) {
                flight={};return Reply::refused;
            }
            for (std::size_t i=0;i<mask.size();++i) if ((mask[i]&flight.live[i])!=0) {
                flight={};return Reply::refused;
            }
            for (std::size_t i=0;i<mask.size();++i) flight.live[i]|=mask[i];
            flight.seen[selector]=true;
            return Reply::accepted;
        }
        if (type!=32) return Reply::refused;
        const auto plan=flight.plan;
        Mask retired{};
        for (std::size_t i=0;i<retired.size();++i) retired[i]=flight.live[i]&plan.lease[i];
        // 32's stored authority mask is not its live-entity mask, so ignore it; also exclude every
        // currently joined allocation even if the lease journal disagrees
        for (const auto& p:history.participants) if (p.joined)
            for (std::size_t i=0;i<retired.size();++i) retired[i]&=~p.lease[i];
        flight={};
        auto result=Ledger::Result::duplicate;
        if (!wire::empty(retired)) result=history.abandon(plan.subject,{retired,0,3});
        if (result==Ledger::Result::capacity) return Reply::capacity;
        if (result==Ledger::Result::refused) return Reply::refused;
        jobs[plan.slot].complete=true;
        return result==Ledger::Result::queued?Reply::queued:Reply::completed;
    }
};
} // namespace dawn::state::activity::entity_retirement
