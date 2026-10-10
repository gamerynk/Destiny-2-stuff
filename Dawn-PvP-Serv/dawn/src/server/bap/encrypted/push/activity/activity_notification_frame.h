#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../internal.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool append_notification_frame(Scratch& scratch,
                                             std::uint64_t sessionId,
                                             std::uint32_t messageType,
                                             std::span<const std::byte> messageBody,
                                             std::span<const std::byte, state::kAesKeySize> key,
                                             std::span<const std::byte, state::kBapNonceSize> nonce,
                                             std::span<std::byte> response,
                                             std::size_t& written) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
