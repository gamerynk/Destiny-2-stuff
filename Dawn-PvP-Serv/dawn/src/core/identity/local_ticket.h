#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::core::identity::local_ticket {

inline constexpr std::size_t kTicketSize = 128;
inline constexpr std::uint32_t kProviderSteam = 1;

struct Claims final {
    std::uint32_t provider{};
    std::uint64_t platformId{};
    std::uint64_t issuedAt{};
    std::uint64_t expiresAt{};
};

[[nodiscard]] bool initialize(void* module) noexcept;

[[nodiscard]] bool issue(std::uint64_t platformId,
                         std::uint64_t nowSeconds,
                         std::span<std::byte, kTicketSize> output) noexcept;

[[nodiscard]] bool authenticate(std::span<const std::byte> signOnBody,
                                std::uint64_t nowSeconds,
                                Claims& claims) noexcept;

void shutdown() noexcept;

} // namespace dawn::core::identity::local_ticket
