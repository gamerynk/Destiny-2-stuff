#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode406 {

inline constexpr std::uint16_t kOpcode = 406;

struct Request {
    std::uint64_t instanceSoid{};
    std::uint16_t definitionIndex{};
    std::uint32_t flags{};
};

// state is biased from INT32_MIN; a refused request keeps whatever fields the parse reached
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode406
