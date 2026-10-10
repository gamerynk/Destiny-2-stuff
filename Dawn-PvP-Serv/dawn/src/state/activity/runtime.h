#pragma once

#include <cstdint>
#include <span>

#include "definition.h"
#include "entity_slots/runtime.h"

namespace dawn::state { struct AccountRuntimeContext; }

namespace dawn::state::activity {
[[nodiscard]] bool prepare_startup_reservations(PendingAllocation& allocation,
    std::span<const membership::Identity> peers) noexcept;

[[nodiscard]] bool snapshot_for_runtime(const AccountRuntimeContext& context,
                                        std::uint64_t accountSoid,
                                        std::uint64_t sessionId,
                                        SessionRecord& output) noexcept;

[[nodiscard]] bool prepare_session(std::uint64_t& sessionId,
                                   PendingAllocation& allocation) noexcept;

[[nodiscard]] bool prepare_session(const destination::DestinationSelection& selection,
                                   std::uint64_t& sessionId,
                                   PendingAllocation& allocation) noexcept;

[[nodiscard]] bool commit(PendingAllocation& allocation) noexcept;

// nothing else clears a record; a host that allocates per region must release them
// or the table fills and the oldest record is evicted
bool release_session(std::uint64_t sessionId) noexcept;

[[nodiscard]] bool contains(std::uint64_t sessionId) noexcept;

[[nodiscard]] bool is_joined(std::uint64_t sessionId) noexcept;

[[nodiscard]] std::uint64_t newest_joined_session() noexcept;

enum class WorldPhase : std::uint8_t {
    idle,
    transitioning,
    arrived,
};

void note_world_phase(WorldPhase phase) noexcept;

[[nodiscard]] WorldPhase world_phase() noexcept;

[[nodiscard]] std::uint64_t world_transition_age() noexcept;

} // namespace dawn::state::activity
