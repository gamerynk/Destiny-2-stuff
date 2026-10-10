#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode1901 {

inline constexpr std::uint16_t kOpcode = 1901;

// the request ends in the whole 64-bit item instance identity plus two zero bits. starter ids (0x4000...)
// also fit an "optional selector" reading, which worked until an item minted as 0x9EAA... was targeted
inline constexpr std::uint64_t kInstanceIdentityMask = 0xFFFFFFFFFFFFFFFFULL;

inline constexpr std::size_t kReplacementCapacity = 12;

struct Replacement {
    std::uint16_t plugDefinitionIndex{};
    std::uint8_t canonicalSocketKind{};
    std::uint8_t modelSocketKind{};
    std::uint32_t socketIndex{};
    std::uint64_t auxiliary{};
};

struct Request {
    std::array<Replacement, kReplacementCapacity> replacements{};
    std::size_t replacementCount{};
    std::uint64_t equipmentSelector{};
    std::uint64_t instanceIdentityToken{};
};

// the selector carries only part of the instance identity, so naming an instance is a comparison, not equality
[[nodiscard]] bool identifies_instance(std::uint64_t instanceIdentityToken,
                                       std::uint64_t instanceSoid) noexcept;

[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode1901
