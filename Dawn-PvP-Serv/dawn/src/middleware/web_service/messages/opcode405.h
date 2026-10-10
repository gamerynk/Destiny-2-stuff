#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode405 {

inline constexpr std::uint16_t kOpcode = 405;

// logical fields of the 136-bit request, lifted off their descriptor biases. layout is wire catalog
// descriptor 0x8080761B; meanings confirmed from two captured kinetic weapon transfers (indices 10849, 6077, quantity 1)
struct Request {
    // int16, bias 0x8000; read 33 on both captured moves, meaning unconfirmed
    std::int16_t unresolved16{};
    // int8, bias 0x80; read 0 on both captured moves, meaning unconfirmed
    std::int8_t unresolved8{};
    std::uint64_t itemInstanceSoid{};
    std::int16_t definitionIndex{};
    std::int32_t quantity{};
};

[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode405
