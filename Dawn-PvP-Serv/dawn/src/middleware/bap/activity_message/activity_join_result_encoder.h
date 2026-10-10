#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::join_result {

inline constexpr std::size_t kEncodedSize = 757;

[[nodiscard]] bool encode_join_result(std::uint32_t correlation,
                                      std::uint64_t sessionId,
                                      std::uint16_t keepaliveHintMs,
                                      std::span<std::byte> output,
                                      std::size_t& written,
                                      std::uint8_t entityEpoch = 0) noexcept;

} // namespace dawn::middleware::bap::activity_message::join_result
