#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode505 {

inline constexpr std::uint16_t kOpcode = 505;
inline constexpr std::size_t kResponseSize = 11;

[[nodiscard]] bool parse_request(const Message& message) noexcept;

[[nodiscard]] bool encode_response(const Message& message,
                                   std::int32_t nextVersion,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode505
