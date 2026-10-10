#pragma once

#include <cstddef>
#include <span>

#include "../../../internal.h"
#include "../../activity_message/definition.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool append_roster_notification(Session& session,
                                              Scratch& scratch,
                                              std::span<const std::byte, state::kAesKeySize> key,
                                              std::array<std::byte, state::kBapNonceSize>& nonce,
                                              std::span<std::byte> response,
                                              std::size_t& written,
                                              bool burst,
                                              const core::protocol::ActivityPlayerTeam* preparedTeam=nullptr) noexcept;

void commit_staged_roster(Session& session) noexcept;

void discard_staged_roster(Session& session) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
