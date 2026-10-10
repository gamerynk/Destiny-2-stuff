#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode601 {

inline constexpr std::uint16_t kOpcode = 601;
inline constexpr std::size_t kResponseSize = 27;

struct Request final {
    std::uint8_t rawKind{};
    std::uint32_t rawSourceTag{}, rawSequence{};
    std::uint64_t sourceHandle{};
};
inline constexpr std::size_t kRequestSize = 17;
[[nodiscard]] bool parse_request(const Message& message, Request& output) noexcept;

[[nodiscard]] bool
encode_response(const Message& message, std::span<std::byte> output, std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode601
