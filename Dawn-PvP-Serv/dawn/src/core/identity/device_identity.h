#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::core::identity::device_identity {

enum class Scope : std::uint32_t {
    enroll = 1,
    signOn = 2,
    discordLinkCode = 3,
    socialSnapshot = 4,
    socialRealtime = 5,
    socialProfile = 6,
    socialRelationships = 7,
    socialMessages = 8,
    socialAvatars = 9,
    missionObservation = 10,
    crucibleMatch = 11,
    accountRename = 12,
    weaponAcquire = 13,
    contentBootstrap = 14,
    transportCertificate = 15,
};

inline constexpr std::size_t kPublicKeySize = 72;
inline constexpr std::size_t kProofSize = 200;

struct Claims final {
    std::uint64_t deviceId{};
    std::uint64_t platformId{};
    std::uint64_t issuedAt{};
    std::array<std::byte, kPublicKeySize> publicKey{};
};

[[nodiscard]] bool initialize(void* module) noexcept;
[[nodiscard]] bool initialize(void* module, std::uint64_t platformId) noexcept;
[[nodiscard]] std::uint64_t platform_id() noexcept;
[[nodiscard]] bool issue(Scope scope,
                         std::uint64_t nowSeconds,
                         std::span<std::byte, kProofSize> output) noexcept;
[[nodiscard]] bool verify(Scope expectedScope,
                          std::span<const std::byte> input,
                          std::uint64_t nowSeconds,
                          Claims& claims) noexcept;
void shutdown() noexcept;

} // namespace dawn::core::identity::device_identity
