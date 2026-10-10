#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode504 {

inline constexpr std::uint16_t kOpcode = 504;

struct Request {
    std::uint64_t characterSoid{};
};

[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode504
