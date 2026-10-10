#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace dawn::middleware::web_service {

inline constexpr std::size_t kEnvelopeHeaderSize = sizeof(std::uint16_t) + sizeof(std::uint32_t);
inline constexpr std::uint8_t kAbsentTrailerWidth = 2;

struct Message {
    std::uint16_t opcode{};
    std::uint32_t transactionId{};
    std::span<const std::byte> payload{};
};

enum class ResponseShape : std::uint8_t {
    statusOnly,
    statusPair,
    statusPairWithBool,
    // header echo with an empty payload for opcodes with no contract; keeps the correlated task from
    // hanging, though the client decoder may under-run
    generic,
};

struct StatusResponse {
    std::int32_t code{};
    std::int32_t value{(std::numeric_limits<std::int32_t>::min)()};
    bool trailingBool{};
};

[[nodiscard]] bool parse_request(std::span<const std::byte> input, Message& message) noexcept;

[[nodiscard]] bool encode_response(const Message& request,
                                   ResponseShape shape,
                                   const StatusResponse& status,
                                   std::span<std::byte> output,
                                   std::size_t& written,
                                   std::span<const std::byte> queuezUpdate = {}) noexcept;

} // namespace dawn::middleware::web_service
