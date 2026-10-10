#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode1820 {

inline constexpr std::uint16_t kOpcode = 1820;

struct Request {
    std::uint16_t collectibleIndex{};
};

// the collectible is optional, so a presence bit precedes its fifteen-bit row; a request naming none is refused
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode1820
