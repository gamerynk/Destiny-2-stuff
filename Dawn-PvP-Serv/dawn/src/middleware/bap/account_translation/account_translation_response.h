#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::account_translation {

inline constexpr std::size_t kMaximumEntries = 256;
using AccountResolver = std::uint64_t (*)(void* context, std::uint64_t platformId) noexcept;

[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   void* context, AccountResolver resolve,
                                   std::span<std::byte> output, std::size_t& written) noexcept;

[[nodiscard]] bool request_platform_id(std::span<const std::byte> requestBody,
                                       std::uint64_t& platformId) noexcept;

// friend roster rows share svc 23; pair only the session's own platform id, not the
// first request
[[nodiscard]] bool request_matches_platform(std::span<const std::byte> requestBody,
                                            std::uint64_t platformId) noexcept;

[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::uint64_t accountSoid,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::account_translation
