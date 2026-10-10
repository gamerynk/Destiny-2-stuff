#pragma once

#include <cstddef>
#include <span>

#include "../../../../state/matchmaking/matchmaking_state.h"

namespace dawn::server::bap::encrypted::matchmaking {

[[nodiscard]] bool encode_response(state::matchmaking::ContextHandle context,
                                   std::span<const std::byte> requestBody,
                                   std::span<std::byte> output,
                                   std::size_t& written,
                                   state::matchmaking::PendingMutation& mutation,
                                   bool& hasMutation) noexcept;

} // namespace dawn::server::bap::encrypted::matchmaking
