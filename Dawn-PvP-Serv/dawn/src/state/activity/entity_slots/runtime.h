#pragma once

#include <cstddef>
#include <cstdint>

#include "../definition.h"
#include "../../runtime/state.h"
#include "../entity_retirement/runtime.h"

namespace dawn::state::activity::entity_slots {

struct PendingMutation final {
    entity_retirement::PendingJoin retirement{};
    entity_retirement::PendingLeaseChange retirementLease{};
    core::protocol::ActivityPeerConnection peerConnection{}, peerConnectionGuard{};
    LeaseMask mask{};
    LeaseMask returnedMask{};
    LeaseMask serverMask{};
    std::uint64_t sessionId{};
    std::uint64_t memberKey{};
    std::uint64_t expectedMemberKey{};
    std::uint64_t expectedStateRevision{};
    std::uint64_t expectedRecordRevision{};
    std::size_t requestedCount{};
    std::size_t serverReserveCount{};
    std::size_t targetSlot{kInvalidSessionSlot};
    MutationKind kind{};
    bool prepared{};
    AccountRuntimeContext sharedSource{};
    SessionRecord sharedRecord{};
    std::uint64_t sharedSourceRevision{};
    std::uint64_t sharedSourceCreatedRevision{};
    std::uint64_t sharedCharacterSoid{};
    membership::Identity privateAdmission{}, privateAdmissionGuard{};
    bool admitsPrivatePeer{};
    bool importsShared{};
};

[[nodiscard]] bool prepare_join(std::uint64_t sessionId,
                                std::uint64_t memberKey,
                                std::size_t grantCount,
                                std::size_t serverReserveCount,
                                PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_grant(std::uint64_t sessionId,
                                 std::size_t requestedCount,
                                 PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_release(std::uint64_t sessionId,
                                   const LeaseMask& returned,
                                   PendingMutation& mutation) noexcept;

[[nodiscard]] bool commit(PendingMutation& mutation) noexcept;

} // namespace dawn::state::activity::entity_slots
