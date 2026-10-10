#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::gameplay::association {

inline constexpr std::size_t kIdentitySize = 8;
inline constexpr std::size_t kSeedSize = 12;
inline constexpr std::size_t kIntegerSize = 128;
// direct IPv4 appends a two-byte clear address trailer after the control bits
inline constexpr std::size_t kTrailerSize = 2;
inline constexpr std::size_t kControlCapacity = 512;

enum class Opcode : std::uint8_t {
    connectRequest = 0,
    keyExchangeOffer = 1,
    keyExchangeResponse = 2,
    reject = 3,
    keepAlive = 4,
    invalid = 5,
};

struct ControlPacket {
    // association word A; an offer advances it on every retry
    std::uint32_t wordA{};
    // association word B; stable across the retries of one attempt
    std::uint32_t wordB{};
    Opcode opcode{Opcode::invalid};
    std::array<std::byte, kIdentitySize> networkId{};
    std::array<std::byte, kIdentitySize> identityNonce{};
    std::array<std::byte, kIntegerSize> verifier{};
    bool hasSignOnBlob{};
    // relayed SignOn material; nothing in the exchange reads it
    std::array<std::byte, kIntegerSize> signOnBlob{};
    std::array<std::byte, kSeedSize> seed{};
    std::array<std::byte, kIntegerSize> publicValue{};
    std::uint32_t responseValue{};
};

// a datagram with no room for the clear trailer is refused; other opcodes decode the header only
[[nodiscard]] bool decode(std::span<const std::byte> datagram, ControlPacket& output) noexcept;

[[nodiscard]] bool encode(const ControlPacket& packet,
                          std::uint16_t destinationPort,
                          std::span<std::byte> output,
                          std::size_t& written) noexcept;

} // namespace dawn::middleware::gameplay::association
