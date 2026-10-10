#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "middleware/bap/activity_message/activity_join_result_encoder.h"

namespace {

namespace join_result = dawn::middleware::bap::activity_message::join_result;

constexpr std::size_t kStatusBit = 570;
constexpr std::size_t kFirstNameBit = 573;
constexpr std::size_t kKeepaliveBit = 1'781;
constexpr std::size_t kHostNameBit = 5'029;
constexpr std::size_t kNameCapacity = 128;

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

[[nodiscard]] std::uint64_t
read_bits(std::span<const std::byte> input, std::size_t bitOffset, std::size_t bitCount) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < bitCount; ++index) {
        const std::size_t sourceBit = bitOffset + index;
        const std::size_t sourceByte = sourceBit / 8;
        const std::size_t sourceShift = 7 - sourceBit % 8;
        const auto byte = static_cast<std::uint8_t>(input[sourceByte]);
        value = (value << 1) | ((byte >> sourceShift) & 1U);
    }
    return value;
}

[[nodiscard]] bool field_is_clear(std::span<const std::byte> body,
                                  std::size_t bitOffset) noexcept {
    for (std::size_t index = 0; index < kNameCapacity; ++index) {
        const auto actual = static_cast<std::uint8_t>(read_bits(body, bitOffset + index * 8, 8));
        if (actual != 0) {
            std::fprintf(stderr,
                         "name field is not clear: bit=%zu index=%zu actual=0x%02X\n",
                         bitOffset,
                         index,
                         static_cast<unsigned>(actual));
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    constexpr std::uint32_t correlation = 0x38B74F7A;
    constexpr std::uint64_t sessionId = 0x9EAA000100200036ULL;
    constexpr std::uint16_t keepalive = 5'000;

    bool passed = true;
    std::array<std::byte, join_result::kEncodedSize> body{};
    std::size_t written = 0;
    passed &= require(join_result::encode_join_result(
                          correlation, sessionId, keepalive, body, written),
                      "accepted join result encodes");
    passed &= require(written == body.size(), "accepted join result keeps its fixed wire size");
    passed &= require(read_bits(body, 0, 32) == correlation,
                      "join result echoes the request correlation");
    passed &= require(read_bits(body, 32, 64) == sessionId,
                      "join result echoes the allocated session id");
    passed &= require(read_bits(body, kStatusBit, 3) == 1,
                      "join result publishes the accepted status");
    passed &= require(field_is_clear(body, kFirstNameBit),
                      "first optional Activity Host session-name field stays clear");
    passed &= require(read_bits(body, kKeepaliveBit, 16) == keepalive,
                      "join result preserves the keepalive hint");
    passed &= require(field_is_clear(body, kHostNameBit),
                      "second optional Activity Host session-name field stays clear");

    std::array<std::byte, join_result::kEncodedSize> rejected{};
    rejected.fill(std::byte{0xA5});
    written = 9;
    passed &= require(!join_result::encode_join_result(
                          correlation, 0, keepalive, rejected, written),
                      "absent activity session is rejected");
    passed &= require(written == 0 && rejected.front() == std::byte{0xA5},
                      "rejected encoding does not modify caller storage");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: captured clear-name activity join-result contract");
    return 0;
}
