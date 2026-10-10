#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::entity_slots {

inline constexpr std::uint32_t kNotificationMessageType = 0;
inline constexpr std::uint32_t kRequestMessageType = 21;
inline constexpr std::size_t kWordCount = 256;
inline constexpr std::size_t kEncodedSize = kWordCount * sizeof(std::uint32_t);
inline constexpr std::size_t kBitsPerMaskByte = 8;
inline constexpr std::size_t kSlotCount = kEncodedSize * kBitsPerMaskByte;

using EntitySlotMask = std::array<std::byte, kEncodedSize>;

[[nodiscard]] bool decode_entity_slots(std::span<const std::byte> input,
                                       EntitySlotMask& mask) noexcept;

[[nodiscard]] bool encode_entity_slots(std::span<const std::byte, kEncodedSize> mask,
                                       std::span<std::byte> output,
                                       std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::activity_message::entity_slots
