#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../internal.h"
#include "definition.h"

namespace dawn::server::bap::encrypted::queuez {

[[nodiscard]] bool stage_service_outcome(Scratch& scratch,
                                         const SessionState& before,
                                         const ServiceOutcome& outcome,
                                         std::span<const std::byte, state::kAesKeySize> key,
                                         std::array<std::byte, state::kBapNonceSize>& nonce,
                                         std::span<std::byte> response,
                                         std::size_t& written,
                                         StagedPublication& publication,
                           InlineSubscriptionUpdate* inlineUpdate = nullptr) noexcept;

} // namespace dawn::server::bap::encrypted::queuez
