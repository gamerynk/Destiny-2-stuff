#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../state/runtime/state.h"

namespace dawn::middleware::secure_channel {

inline constexpr std::size_t kServerHelloEnvelopeSize = 84;

inline constexpr std::size_t kFrameTagSize = 16;

[[nodiscard]] bool encode_server_hello(const state::SignOnState& signOn,
                                       const state::BapState& bap,
                                       std::span<std::byte> output,
                                       std::size_t& written) noexcept;

[[nodiscard]] bool seal_frame(std::span<const std::byte, state::kAesKeySize> key,
                              std::span<const std::byte, state::kBapNonceSize> nonce,
                              std::span<const std::byte> plaintext,
                              std::span<std::byte> output,
                              std::size_t& written) noexcept;

[[nodiscard]] bool open_frame(std::span<const std::byte, state::kAesKeySize> key,
                              std::span<const std::byte, state::kBapNonceSize> nonce,
                              std::span<const std::byte> payload,
                              std::span<std::byte> output,
                              std::size_t& written) noexcept;

void advance_nonce(std::span<std::byte, state::kBapNonceSize> nonce) noexcept;

} // namespace dawn::middleware::secure_channel
