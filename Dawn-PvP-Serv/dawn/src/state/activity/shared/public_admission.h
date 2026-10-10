#pragma once
#include "runtime.h"
#include "public_member_connection_policy.h"

namespace dawn::state::activity::shared {
inline constexpr std::uint8_t kPublicLeaseParts=32;
// PRIVATE ids stay account-scoped, but PUBLIC ids must not collide with any local record
std::uint64_t available_session_counter_locked(std::uint64_t base,std::uint64_t first,bool publicAllocation) noexcept;
bool prepare_public_allocator(PendingAllocation& allocation) noexcept;
bool compatible_public_destination(const destination::DestinationSelection& a,
                                   const destination::DestinationSelection& b) noexcept;
bool admit_public_source(const AccountRuntimeContext& owner,std::uint64_t host,
                         const AccountRuntimeContext& caller,std::uint64_t source) noexcept;
bool public_source_snapshot(std::uint64_t host,std::uint64_t account,SessionRecord& source) noexcept;
bool public_source_snapshots(std::uint64_t host,std::array<SessionRecord,32>& sources) noexcept;
bool public_member_connection(std::uint64_t host,std::uint64_t memberKey,std::uint64_t playerId,
                              core::protocol::ActivityPeerConnection& output) noexcept;
bool public_member_connection(std::uint64_t host,std::uint64_t memberKey,std::uint64_t playerId,
                              core::protocol::ActivityPeerConnection& output,
                              const PublicMemberConnectionFence& expected) noexcept;
bool public_join_authorized(std::uint64_t host,std::uint64_t key,std::uint64_t character) noexcept;
bool valid_public_join_locked(const SessionRecord& host,std::uint64_t account,std::uint64_t key,
                              std::uint64_t character=0) noexcept;
bool public_peers(std::uint64_t firstAccount,std::uint64_t secondAccount) noexcept;
}
