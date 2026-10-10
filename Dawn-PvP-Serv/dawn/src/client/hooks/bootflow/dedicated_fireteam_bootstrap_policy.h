#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../middleware/gameplay/descriptor/join_descriptor.h"

namespace dawn::client::hooks::bootflow::dedicated_fireteam_bootstrap_policy {

inline constexpr std::size_t kPrimaryAccessorSignatureSize = 56;

namespace detail {

template <typename Value>
[[nodiscard]] constexpr Value read_little_endian(
    const std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize>& bytes,
    std::size_t offset) noexcept {
    Value value{};
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        value |= static_cast<Value>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

} // namespace detail

[[nodiscard]] constexpr bool primary_accessor_is_pc_1_59(
    const std::array<std::byte, kPrimaryAccessorSignatureSize>& code) noexcept {
    constexpr std::array<std::byte, kPrimaryAccessorSignatureSize> expected{
        std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
        std::byte{0xEC}, std::byte{0x20}, std::byte{0x48}, std::byte{0x63},
        std::byte{0xD9}, std::byte{0xE8}, std::byte{},     std::byte{},
        std::byte{},     std::byte{},     std::byte{0x48}, std::byte{0x85},
        std::byte{0xC0}, std::byte{0x74}, std::byte{},     std::byte{0x83},
        std::byte{0xFB}, std::byte{0xFF}, std::byte{0x74}, std::byte{},
        std::byte{0x4C}, std::byte{0x69}, std::byte{0xC3}, std::byte{0x48},
        std::byte{0x91}, std::byte{0x03}, std::byte{0x00}, std::byte{0x4C},
        std::byte{0x03}, std::byte{0xC0}, std::byte{0x49}, std::byte{0x63},
        std::byte{0x40}, std::byte{},     std::byte{0x48}, std::byte{0x69},
        std::byte{0xC8}, std::byte{0xA0}, std::byte{0xC8}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x49}, std::byte{0x8D}, std::byte{0x40},
        std::byte{},     std::byte{0x48}, std::byte{0x03}, std::byte{0xC1},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xC4}, std::byte{0x20},
    };
    constexpr std::array<std::size_t, 8> wildcards{10, 11, 12, 13, 18, 23, 37, 48};
    for (std::size_t index = 0; index < code.size(); ++index) {
        bool wildcard = false;
        for (const std::size_t candidate : wildcards) {
            wildcard |= candidate == index;
        }
        if (!wildcard && code[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

// the native descriptor may select any retail transport method; the central service only
// relays these bytes and never rewrites the address or join key
[[nodiscard]] constexpr bool descriptor_is_native_owner(
    const std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize>& descriptor,
    std::uint64_t lobbyId) noexcept {
    constexpr std::size_t kMachineOffset = 0;
    constexpr std::size_t kNetAddrOffset = 8;
    constexpr std::size_t kMethodOffset =
        kNetAddrOffset + middleware::gameplay::descriptor::kNetAddrSize - 1U;
    constexpr std::size_t kOnlineSessionOffset = 110;

    const std::uint64_t machine =
        detail::read_little_endian<std::uint64_t>(descriptor, kMachineOffset);
    const std::uint64_t onlineSession =
        detail::read_little_endian<std::uint64_t>(descriptor, kOnlineSessionOffset);
    const std::uint8_t method =
        std::to_integer<std::uint8_t>(descriptor[kMethodOffset]);
    bool netAddrPresent = false;
    for (std::size_t index = 0; index < middleware::gameplay::descriptor::kNetAddrSize;
         ++index) {
        netAddrPresent |= descriptor[kNetAddrOffset + index] != std::byte{};
    }
    return lobbyId != 0 && machine != 0 && onlineSession == lobbyId && method <= 7U
           && netAddrPresent;
}

} // namespace dawn::client::hooks::bootflow::dedicated_fireteam_bootstrap_policy
