// clock rule checks skew and freshness only; replay needs committed purchases and none exist yet

#include "opcode901_codec.h"

#include "../../../encoding/bit_reader.h"

namespace dawn::middleware::web_service::messages::opcode901 {
namespace {

constexpr std::uint8_t kIndexWidth = 16;
constexpr std::int32_t kIndexBias = 0x8000;
constexpr std::uint8_t kClockWidth = 64;
constexpr std::uint8_t kPresenceWidth = 1;
// both legal forms end mid byte so up to 7 pad bits are fine; a whole spare byte is data
constexpr std::size_t kPaddingLimit = 8;

[[nodiscard]] bool read_index(encoding::bits::Reader& reader, std::int16_t& output) noexcept {
    std::uint64_t stored = 0;
    if (!reader.read(kIndexWidth, stored)) {
        return false;
    }
    output = static_cast<std::int16_t>(static_cast<std::int32_t>(stored) - kIndexBias);
    return true;
}

} // namespace

bool parse_request(const Message& message, Request& output) noexcept {
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    Request candidate{};
    std::uint64_t present = 0;
    if (!read_index(reader, candidate.vendorIndex) || !read_index(reader, candidate.saleIndex)
        || !reader.read(kPresenceWidth, present)) {
        return false;
    }
    candidate.hasClock = present != 0;
    if (candidate.hasClock) {
        std::uint64_t clock = 0;
        if (!reader.read(kClockWidth, clock)) {
            return false;
        }
        candidate.clock = static_cast<std::int64_t>(clock);
    }
    if (reader.remaining_bits() >= kPaddingLimit) {
        return false;
    }
    output = candidate;
    return true;
}

ClockPolicy check_clock(const Request& request, std::int64_t serverClock) noexcept {
    if (!request.hasClock) {
        return ClockPolicy::absent;
    }
    if (request.clock > serverClock) {
        const std::uint64_t ahead =
            static_cast<std::uint64_t>(request.clock) - static_cast<std::uint64_t>(serverClock);
        return ahead > kClockAheadLimitSeconds ? ClockPolicy::ahead : ClockPolicy::accepted;
    }
    const std::uint64_t behind =
        static_cast<std::uint64_t>(serverClock) - static_cast<std::uint64_t>(request.clock);
    return behind > kClockBehindLimitSeconds ? ClockPolicy::stale : ClockPolicy::accepted;
}

const char* clock_policy_name(ClockPolicy policy) noexcept {
    switch (policy) {
    case ClockPolicy::accepted:
        return "ok";
    case ClockPolicy::absent:
        return "absent";
    case ClockPolicy::ahead:
        return "ahead";
    case ClockPolicy::stale:
        return "stale";
    }
    return "unknown";
}

} // namespace dawn::middleware::web_service::messages::opcode901
