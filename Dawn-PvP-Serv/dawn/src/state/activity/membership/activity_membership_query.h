#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../definition.h"

namespace dawn::state::activity::membership {

[[nodiscard]] core::protocol::ActivityPlayerTeam player_team(std::uint64_t sessionId) noexcept;

struct PendingMutation final {
    Snapshot snapshot{};
    Identity identityGuard{};
    Reservations reservationsGuard{};
    AuthoritativeUpdate authoritativeInput{};
    AuthoritativeUpdate authoritativeGuard{};
    std::uint64_t sessionId{};
    std::uint64_t expectedStateRevision{};
    std::uint64_t expectedRecordRevision{};
    std::uint64_t expectedPrimarySoid{};
    std::uint64_t refreshRequestGuard{};
    std::uint32_t requestedRevision{};
    std::uint32_t acknowledgement{};
    std::int32_t bubbleIndex{};
    std::size_t targetSlot{kInvalidSessionSlot};
    MutationKind kind{};
    bool hasSnapshot{};
    bool changesState{};
    bool movesRegion{};
    bool movesTransitionToken{};
    bool prepared{};
};

[[nodiscard]] bool prepare_reservations(std::uint64_t sessionId, std::uint32_t epoch,
    std::span<const Identity> peers, std::uint64_t releaseKey, PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_identity(std::uint64_t sessionId,
                                    const Identity& identity,
                                    PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_authoritative(std::uint64_t sessionId,
                                         const AuthoritativeUpdate& update,
                                         PendingMutation& mutation) noexcept;

[[nodiscard]] bool prepare_refresh(std::uint64_t sessionId,
                                   std::uint32_t requestedRevision,
                                   std::int32_t bubbleIndex,
                                   PendingMutation& mutation) noexcept;

[[nodiscard]] bool acknowledged(std::uint64_t sessionId) noexcept;

// the client applies one update per revision and drops repeats,
// so a corrected body needs a new revision
[[nodiscard]] bool republish(std::uint64_t sessionId) noexcept;

[[nodiscard]] std::int32_t reported_region(std::uint64_t sessionId) noexcept;

[[nodiscard]] std::uint64_t live_region_session(std::uint64_t fallback) noexcept;

[[nodiscard]] std::uint64_t member_key(std::uint64_t sessionId) noexcept;

[[nodiscard]] std::uint64_t join_identity(std::uint64_t sessionId) noexcept;

[[nodiscard]] std::uint64_t account_soid(std::uint64_t sessionId) noexcept;

[[nodiscard]] bool prepare_acknowledgement(std::uint64_t sessionId,
                                           std::uint32_t revision,
                                           PendingMutation& mutation) noexcept;

[[nodiscard]] bool commit(PendingMutation& mutation) noexcept;

} // namespace dawn::state::activity::membership
