#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/gameplay/definition.h"
#include "control_packet.h"

namespace dawn::middleware::gameplay::association {

// the envelope keys AES-128, so only the first 16 bytes of the derived key are used
inline constexpr std::size_t kKeySize = state::gameplay::kChannelKeySize;
inline constexpr std::size_t kNonceSize = kSeedSize;
inline constexpr std::size_t kTagSize = 16;
inline constexpr std::size_t kProtectedCapacity = 1500;
// length fields are 11 bits, so a payload must stay below 2048
inline constexpr std::size_t kMaximumPayload = 2047;

using ProtectedContext = state::gameplay::ProtectedContext;

// the clear header is not authenticated; the tag covers the encrypted bits only
[[nodiscard]] bool seal(ProtectedContext& context,
                        std::span<const std::byte> payload,
                        std::uint16_t destinationPort,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept;

// updates no sender state; the caller applies replay policy after success
[[nodiscard]] bool open(const ProtectedContext& context,
                        std::span<const std::byte> datagram,
                        std::span<std::byte> payload,
                        std::size_t& payloadSize,
                        std::uint32_t& wordA) noexcept;

} // namespace dawn::middleware::gameplay::association
