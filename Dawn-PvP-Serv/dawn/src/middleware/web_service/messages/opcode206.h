#pragma once

#include "../../queuez/subscription.h"
#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode206 {

inline constexpr std::uint16_t kOpcode = 206;

[[nodiscard]] bool parse_request(const Message& message,
                                 queuez::Subscription& subscription) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode206
