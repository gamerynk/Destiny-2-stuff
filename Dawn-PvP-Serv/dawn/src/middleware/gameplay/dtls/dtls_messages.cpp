#include "dtls_messages.h"

#include <algorithm>

namespace dawn::middleware::gameplay::dtls {

namespace {

constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFF;

constexpr std::size_t kTypeOffset = 0;
// the requester writes this and never reads it back
constexpr std::size_t kVersionOffset = 1;
constexpr std::byte kVersion{2};
constexpr std::size_t kPeerTagOffset = 2;
// four reserved bytes close the header; both handshake packets leave them zero
constexpr std::size_t kReservedOffset = 4;

constexpr std::size_t kInitTagOffset = kHeaderSize;
constexpr std::size_t kInitSecurityIdOffset = kInitTagOffset + 2;

constexpr std::size_t kInitAckTimestampOffset = kHeaderSize;
constexpr std::size_t kInitAckCookieOffset = kInitAckTimestampOffset + 4;
// five tags follow the cookie: responder, responder, requester, then two migration tags
constexpr std::size_t kInitAckTagsOffset = kInitAckCookieOffset + kCookieSize;
constexpr std::size_t kInitAckAddressOffset = kInitAckTagsOffset + 10;
constexpr std::size_t kInitAckSecurityIdOffset = kInitAckAddressOffset + 6;

constexpr std::size_t kCookieEchoInitAckOffset = kHeaderSize;
constexpr std::size_t kCookieEchoSecurityIdOffset = kCookieEchoInitAckOffset + kInitAckSize + 41;
constexpr std::size_t kCookieEchoKeyOffset = kCookieEchoSize - kPublicKeySize;

constexpr std::size_t kCookieAckKeyOffset = kHeaderSize;
constexpr std::size_t kCookieAckSecurityIdOffset = kCookieAckKeyOffset + kPublicKeySize;

void write_memory_order(std::span<std::byte> output,
                        std::size_t offset,
                        std::uint64_t value,
                        std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        output[offset + index] = static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
    }
}

[[nodiscard]] std::uint16_t read_word(std::span<const std::byte> datagram,
                                      std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(datagram[offset])
        | (std::to_integer<std::uint16_t>(datagram[offset + 1]) << kByteBits));
}

void write_header(std::span<std::byte> output, Type type, std::uint16_t peerTag) noexcept {
    output[kTypeOffset] = static_cast<std::byte>(type);
    output[kVersionOffset] = kVersion;
    write_memory_order(output, kPeerTagOffset, peerTag, sizeof(std::uint16_t));
    write_memory_order(output, kReservedOffset, 0, sizeof(std::uint32_t));
}

void write_address(std::span<std::byte> output,
                   std::size_t offset,
                   std::uint32_t address,
                   std::uint16_t port) noexcept {
    // address goes out in dotted order, the port beside it low byte first
    output[offset] = static_cast<std::byte>((address >> 24U) & kByteMask);
    output[offset + 1] = static_cast<std::byte>((address >> 16U) & kByteMask);
    output[offset + 2] = static_cast<std::byte>((address >> kByteBits) & kByteMask);
    output[offset + 3] = static_cast<std::byte>(address & kByteMask);
    write_memory_order(output, offset + 4, port, sizeof(std::uint16_t));
}

} // namespace

bool read_type(std::span<const std::byte> datagram, std::uint8_t& output) noexcept {
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    output = std::to_integer<std::uint8_t>(datagram[kTypeOffset]);
    return true;
}

bool read_peer_tag(std::span<const std::byte> datagram, std::uint16_t& output) noexcept {
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    output = read_word(datagram, kPeerTagOffset);
    return true;
}

bool read_init(std::span<const std::byte> datagram, Init& output) noexcept {
    std::uint8_t type = 0;
    if (datagram.size() != kInitSize || !read_type(datagram, type)
        || type != static_cast<std::uint8_t>(Type::init)) {
        return false;
    }
    output.initTag = read_word(datagram, kInitTagOffset);
    std::copy_n(
        datagram.begin() + kInitSecurityIdOffset, kSecurityIdSize, output.securityId.begin());
    return true;
}

bool read_cookie_echo(std::span<const std::byte> datagram, CookieEcho& output) noexcept {
    std::uint8_t type = 0;
    if (datagram.size() != kCookieEchoSize || !read_type(datagram, type)
        || type != static_cast<std::uint8_t>(Type::cookieEcho)) {
        return false;
    }
    std::copy_n(
        datagram.begin() + kCookieEchoInitAckOffset, kInitAckSize, output.echoedInitAck.begin());
    std::copy_n(
        datagram.begin() + kCookieEchoSecurityIdOffset, kSecurityIdSize, output.securityId.begin());
    std::copy_n(datagram.begin() + kCookieEchoKeyOffset, kPublicKeySize, output.publicKey.begin());
    return true;
}

void write_init_ack(const InitAck& initAck, std::array<std::byte, kInitAckSize>& output) noexcept {
    output = {};
    write_header(output, Type::initAck, initAck.requesterTag);
    write_memory_order(output, kInitAckTimestampOffset, initAck.timestamp, sizeof(std::uint32_t));
    std::copy(initAck.cookie.begin(), initAck.cookie.end(), output.begin() + kInitAckCookieOffset);
    // requester adopts the first tag as its peer tag and checks the third against its own
    write_memory_order(output, kInitAckTagsOffset, initAck.responderTag, sizeof(std::uint16_t));
    write_memory_order(output, kInitAckTagsOffset + 2, initAck.responderTag, sizeof(std::uint16_t));
    write_memory_order(output, kInitAckTagsOffset + 4, initAck.requesterTag, sizeof(std::uint16_t));
    // last two tags carry a migrating association's old pair; a fresh one leaves them zero
    write_address(output, kInitAckAddressOffset, initAck.address, initAck.port);
    std::copy(initAck.securityId.begin(),
              initAck.securityId.end(),
              output.begin() + kInitAckSecurityIdOffset);
}

void write_cookie_ack(const CookieAck& cookieAck,
                      std::array<std::byte, kCookieAckSize>& output) noexcept {
    output = {};
    write_header(output, Type::cookieAck, cookieAck.requesterTag);
    std::copy(cookieAck.publicKey.begin(),
              cookieAck.publicKey.end(),
              output.begin() + kCookieAckKeyOffset);
    std::copy(cookieAck.securityId.begin(),
              cookieAck.securityId.end(),
              output.begin() + kCookieAckSecurityIdOffset);
}

} // namespace dawn::middleware::gameplay::dtls
