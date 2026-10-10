#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode402 {

inline constexpr std::uint16_t kOpcode = 402;
// the descriptor only ever names a single unit, whatever the stack holds
inline constexpr std::uint32_t kSingleQuantity = 1;

struct Request {
    std::uint64_t instanceSoid{};
    std::uint16_t definitionIndex{};
};

// quantity, required flag and all three pad runs are fixed for this action; a descriptor that differs
// is unsupported. fields are filled as far as the parse gets, so a refused request keeps its trace
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode402
