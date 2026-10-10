#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../../../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../../internal.h"

namespace dawn::server::bap::encrypted::push::activity {

namespace message = middleware::bap::activity_message::sensor_auth_update;

[[nodiscard]] bool seed_identity(std::uint64_t sessionId,
                                 std::uint64_t memberKey,
                                 std::uint64_t characterSoid) noexcept;

[[nodiscard]] bool seed_transition_token(std::uint64_t sessionId) noexcept;

[[nodiscard]] bool seed_transition_region(std::uint64_t sessionId,
                                          std::int32_t regionIndex) noexcept;

enum class RosterOutcome : std::uint8_t {
    published,
    noEpoch,
    noLayout,
    noGroups,
    encodeFailed,
};

[[nodiscard]] RosterOutcome build_roster_snapshot(Session& session,
                                                  Scratch& scratch,
                                                  message::Snapshot& snapshot,
                                                  std::span<char> destination,
                                                  std::size_t& destinationLength,
                                                  bool burst,
                                                  const core::protocol::ActivityPlayerTeam* preparedTeam=nullptr) noexcept;

void report_roster_push(Session& session,
                        const message::Snapshot& snapshot,
                        std::string_view destination,
                        std::size_t bytes,
                        std::int32_t grant,
                        RosterOutcome outcome) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
