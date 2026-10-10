#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../activity_message/definition.h"
#include "../internal.h"

namespace dawn::server::bap::encrypted::activity_transaction {

[[nodiscard]] bool stage_notifications(Session& session,
                                       Scratch& scratch,
                                       const activity_message::ActivityPlan& activity,
                                       std::span<const std::byte, state::kAesKeySize> key,
                                       std::array<std::byte, state::kBapNonceSize>& nonce,
                                       std::span<std::byte> response,
                                       std::size_t& written) noexcept;

} // namespace dawn::server::bap::encrypted::activity_transaction
