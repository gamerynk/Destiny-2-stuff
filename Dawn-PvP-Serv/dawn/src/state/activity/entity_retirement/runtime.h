#pragma once

#include "policy.h"
#include "departure_query.h"

namespace dawn::state::activity::entity_retirement {
using History=Journal<>;
using Queries=DepartureQueries<History>;
struct PendingJoin {
    std::uint64_t activity{};
    History::Join join{};
    bool managed{};
};
struct PendingLeaseChange {
    std::uint64_t activity{};
    History::LeaseChange change{};
    bool managed{};
};
bool prepare_lease_change_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
    const wire::Mask& before,const wire::Mask& after,PendingLeaseChange& out) noexcept;
bool can_commit_lease_change_locked(const PendingLeaseChange& plan,
    const wire::Mask& before,const wire::Mask& after) noexcept;
void commit_lease_change_locked(const PendingLeaseChange& plan,
    const wire::Mask& before,const wire::Mask& after) noexcept;
bool prepare_join(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
                  std::span<const std::byte,1024> lease,PendingJoin& output) noexcept;
bool can_commit_join_locked(const PendingJoin& plan) noexcept;
void commit_join_locked(const PendingJoin& plan) noexcept;
void depart_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t member,
                    std::span<const std::byte,1024> lease) noexcept;
void reset_locked() noexcept;
bool matches_locked(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision) noexcept;
bool close_view(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision) noexcept;
bool next_query(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
                std::uint64_t now,Queries::Plan& out) noexcept;
bool publish_query(std::uint64_t activity,const Queries::Plan& plan,std::uint64_t now,
                   void (*copy)(void*) noexcept,void* context) noexcept;
Queries::Reply answer_query(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
                            unsigned type,std::span<const std::byte> body,std::uint64_t now) noexcept;
History::Result abandon(std::uint64_t activity,std::uint64_t account,
                         const wire::Abandon& release) noexcept;
bool join_operation(const PendingJoin& plan,std::uint64_t serial,History::Operation& out) noexcept;
bool next(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
           History::Operation& out) noexcept;
// callback must not throw, copies already-staged bytes/nonce only, and must not call State
bool publish(std::uint64_t activity,std::uint64_t account,std::uint64_t joinRevision,
              std::uint64_t serial,void (*copy)(void*) noexcept,void* context) noexcept;
}
