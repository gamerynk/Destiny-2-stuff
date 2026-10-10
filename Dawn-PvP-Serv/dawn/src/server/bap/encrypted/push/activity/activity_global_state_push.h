#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../../middleware/bap/activity_message/activity_global_state_encoder.h"
#include "../../../../../state/activity/destination/definition.h"
#include "../../../internal.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool
resolve_state(std::uint64_t sessionId,
              middleware::bap::activity_message::global_activity_state::GlobalActivityState& output,
              state::activity::destination::DestinationSelection& selection,
              const state::activity::destination::DestinationSelection* planned=nullptr) noexcept;

[[nodiscard]] bool
append_global_state_notification(Scratch& scratch,
                                 std::uint64_t sessionId,
                                 std::span<const std::byte, state::kAesKeySize> key,
                                 std::array<std::byte, state::kBapNonceSize>& nonce,
                                 std::span<std::byte> response,
                                 std::size_t& written,
                                 const state::activity::destination::DestinationSelection* planned=nullptr) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
