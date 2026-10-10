#pragma once

#include <cstddef>
#include <cstdint>

#include "../../web_service_envelope.h"

namespace dawn::middleware::web_service::messages::opcode901 {

inline constexpr std::uint16_t kOpcode = 901;

struct Request {
    std::int16_t vendorIndex{};
    std::int16_t saleIndex{};
    std::int64_t clock{};
    bool hasClock{};
};

enum class ClockPolicy : std::uint8_t {
    accepted,
    absent,
    ahead,
    stale,
};

inline constexpr std::uint64_t kClockAheadLimitSeconds = 60;
inline constexpr std::uint64_t kClockBehindLimitSeconds = 300;

[[nodiscard]] bool parse_request(const Message& message, Request& output) noexcept;

[[nodiscard]] ClockPolicy check_clock(const Request& request, std::int64_t serverClock) noexcept;

[[nodiscard]] const char* clock_policy_name(ClockPolicy policy) noexcept;

} // namespace dawn::middleware::web_service::messages::opcode901
