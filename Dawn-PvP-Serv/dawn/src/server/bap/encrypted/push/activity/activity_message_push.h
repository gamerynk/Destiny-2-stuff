#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../internal.h"
#include "../../activity_message/definition.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool append_join_notifications(Scratch& scratch,
                                             const activity_message::ActivityPlan& activity,
                                             std::span<const std::byte, state::kAesKeySize> key,
                                             std::array<std::byte, state::kBapNonceSize>& nonce,
                                             std::span<std::byte> response,
                                             std::size_t& written) noexcept;

[[nodiscard]] bool
append_entity_slot_notification(Scratch& scratch,
                                std::uint64_t sessionId,
                                std::span<const std::byte> entitySlots,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
