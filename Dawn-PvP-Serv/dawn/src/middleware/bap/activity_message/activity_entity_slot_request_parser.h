#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::entity_slot_request {

inline constexpr std::uint32_t kMessageType = 20;
inline constexpr std::size_t kEncodedSize = sizeof(std::uint32_t);

[[nodiscard]] bool parse_entity_slot_request(std::span<const std::byte> input,
                                             std::int32_t& value) noexcept;

} // namespace dawn::middleware::bap::activity_message::entity_slot_request
