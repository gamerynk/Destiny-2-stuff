#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode501 {

inline constexpr std::uint16_t kOpcode = 501;

inline constexpr std::size_t kIdentityHeaderSize = 36;

struct Request {
    std::uint8_t race{};
    std::uint8_t gender{};
    std::uint8_t characterClass{};
    std::array<std::uint8_t, kIdentityHeaderSize> identityHeader{};
};

[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

[[nodiscard]] bool encode_response(const Message& message,
                                   std::int32_t nextVersion,
                                   std::uint64_t characterSoid,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode501
