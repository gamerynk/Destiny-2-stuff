#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode403 {

inline constexpr std::uint16_t kOpcode = 403;
inline constexpr std::uint16_t kUnequipOpcode = 404;

struct Request {
    std::uint64_t instanceSoid{};
};

// equip and unequip name one item instance and differ only in direction, so they share this
// descriptor; the trailing byte is a required alignment pad and must be clear
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode403
