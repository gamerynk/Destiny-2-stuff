#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/investment/investment.h"
#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode205 {

inline constexpr std::uint16_t kOpcode = 205;

// no response field reads the request body, so its width is not tested
[[nodiscard]] bool parse_request(const Message& message) noexcept;

[[nodiscard]] bool encode_response(const Message& message,
                                   const state::InvestmentState& investment,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode205
