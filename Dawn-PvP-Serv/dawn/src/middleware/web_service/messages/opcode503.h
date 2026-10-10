#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/investment/investment.h"
#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode503 {

inline constexpr std::uint16_t kOpcode = 503;

struct Request {
    std::uint8_t tag{};
    std::uint64_t primarySoid{};
    bool hasPrimarySoid{};
};

// a short body or zero key clears hasPrimarySoid but still replies; an empty reply under-runs the client decoder
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

[[nodiscard]] bool encode_response(const Message& message,
                                   const Request& request,
                                   const state::InvestmentState& investment,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode503
