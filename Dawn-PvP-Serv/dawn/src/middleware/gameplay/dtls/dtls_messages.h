#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::gameplay::dtls {

inline constexpr std::size_t kHeaderSize = 8;
inline constexpr std::size_t kSecurityIdSize = 8;
inline constexpr std::size_t kInitSize = 18;
inline constexpr std::size_t kInitAckSize = 52;
inline constexpr std::size_t kCookieSize = 16;
// exported public key is padded to this length whatever the curve produced
inline constexpr std::size_t kPublicKeySize = 100;
inline constexpr std::size_t kCookieEchoSize =
    kHeaderSize + kInitAckSize + 41 + kSecurityIdSize + kPublicKeySize;
inline constexpr std::size_t kCookieAckSize = kHeaderSize + kPublicKeySize + kSecurityIdSize;

using SecurityId = std::array<std::byte, kSecurityIdSize>;

enum class Type : std::uint8_t {
    init = 1,
    initAck = 2,
    cookieEcho = 3,
    cookieAck = 4,
};

struct Init {
    std::uint16_t initTag{};
    // every later packet we send must repeat this or the peer discards it
    SecurityId securityId{};
};

struct CookieEcho {
    // the responder's own init ack, returned unaltered; comparing it is the cookie check
    std::array<std::byte, kInitAckSize> echoedInitAck{};
    std::array<std::byte, kPublicKeySize> publicKey{};
    SecurityId securityId{};
};

struct InitAck {
    std::uint16_t requesterTag{};
    std::uint16_t responderTag{};
    std::array<std::byte, kCookieSize> cookie{};
    std::uint32_t address{};
    std::uint16_t port{};
    std::uint32_t timestamp{};
    SecurityId securityId{};
};

struct CookieAck {
    std::uint16_t requesterTag{};
    std::array<std::byte, kPublicKeySize> publicKey{};
    SecurityId securityId{};
};

[[nodiscard]] bool read_type(std::span<const std::byte> datagram, std::uint8_t& output) noexcept;

[[nodiscard]] bool read_peer_tag(std::span<const std::byte> datagram,
                                 std::uint16_t& output) noexcept;

[[nodiscard]] bool read_init(std::span<const std::byte> datagram, Init& output) noexcept;

[[nodiscard]] bool read_cookie_echo(std::span<const std::byte> datagram,
                                    CookieEcho& output) noexcept;

void write_init_ack(const InitAck& initAck, std::array<std::byte, kInitAckSize>& output) noexcept;

void write_cookie_ack(const CookieAck& cookieAck,
                      std::array<std::byte, kCookieAckSize>& output) noexcept;

} // namespace dawn::middleware::gameplay::dtls
