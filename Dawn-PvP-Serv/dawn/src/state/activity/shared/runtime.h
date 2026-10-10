#pragma once
#include "../../runtime/state.h"
#include "../entity_slots/runtime.h"

namespace dawn::state::activity::shared {
// raised from 6 for 12-player fireteams; each lane gets about 682 of the 8192 entity slots
inline constexpr std::uint8_t kPrivateLeaseParts=12;
[[nodiscard]] bool private_reservations_locked(const SessionRecord& root,
    std::array<membership::Identity,31>& rows,bool fill) noexcept;
[[nodiscard]] bool context_available_locked(const AccountRuntimeContext& context) noexcept;
void register_context_locked(const AccountRuntimeContext& context) noexcept;
void reset_locked() noexcept;
[[nodiscard]] bool canonical_absent(std::uint64_t sessionId) noexcept;
void disconnect(const AccountRuntimeContext& context,std::uint64_t sessionId,
                std::uint64_t expectedJoinRevision=0) noexcept;
[[nodiscard]] bool disconnect_member(std::uint64_t sessionId,std::uint64_t memberKey) noexcept;
void notify_changed_locked(const ActivityState& before,ActivityState& after) noexcept;
[[nodiscard]] bool valid_identity(const SessionRecord& record,const membership::Identity& identity) noexcept;
[[nodiscard]] bool owner_context(std::uint64_t sessionId, AccountRuntimeContext& output) noexcept;
[[nodiscard]] bool canonical_context(std::uint64_t sessionId, AccountRuntimeContext& output) noexcept;
[[nodiscard]] bool participant_snapshot(const SessionRecord& root, std::uint64_t accountSoid,
                                      SessionRecord& output) noexcept;
[[nodiscard]] bool participant_accounts_snapshot(const SessionRecord& root,
                                                 std::array<std::uint64_t,32>& accounts) noexcept;
[[nodiscard]] bool has_joined_participants(const AccountRuntimeContext& context,
                                            std::uint64_t accountSoid,
                                            std::uint64_t sessionId) noexcept;
[[nodiscard]] std::uint8_t lease_parts(const SessionRecord& record) noexcept;
[[nodiscard]] entity_slots::LeaseMask excluded_slots(const SessionRecord& record,
                                                    std::size_t reserve) noexcept;
[[nodiscard]] entity_slots::LeaseMask allocation_exclusions_locked(
    const SessionRecord& record, std::size_t reserve, bool joining) noexcept;
void note_slot_lease_locked(const SessionRecord& record,
    const entity_slots::LeaseMask& mask, bool returned=false) noexcept;

enum class JoinFailure { none, validation, reservation, capacity, reserve, lease };
[[nodiscard]] bool prepare_join(const AccountRuntimeContext& source,
                               std::uint64_t sessionId, std::uint64_t memberKey,
                               std::uint64_t characterSoid, std::size_t requested,
                               std::size_t reserve, entity_slots::PendingMutation& plan,
                               JoinFailure* failure=nullptr,
                               const membership::Identity* authorizedPrivateIdentity=nullptr) noexcept;
[[nodiscard]] bool commit_join(entity_slots::PendingMutation& plan) noexcept;

struct Projection {
    std::array<core::protocol::ActivityPeerConnection,32> peerConnections{};
    std::array<membership::Identity,32> members{};
    std::array<std::uint8_t,32> tokens{};
    membership::SpawnState spawn{};
    membership::TeleportState teleport{};
    std::uint64_t hostKey{};
    std::uint8_t localSlot{};
    std::uint32_t identityWireWordMask{};
    std::uint32_t joinedMemberMask{};
};
inline membership::Identity as_reservation(membership::Identity identity) noexcept {
    std::uint64_t reversed=0;
    for (unsigned byte=0;byte<8;++byte)
        reversed=(reversed<<8)|((identity.joinIdentity>>(byte*8))&255);
    identity.joinIdentity=reversed;
    return identity;
}
[[nodiscard]] bool project(std::uint64_t sessionId, const membership::Snapshot& self,
                           Projection& output, std::uint64_t peerSourceSessionId=0) noexcept;
}
