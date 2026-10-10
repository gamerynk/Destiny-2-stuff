#pragma once
#include <cstdint>
#include "completion_timing.h"
namespace dawn::state::activity::crucible {
struct NativeCountdownPublication final {
    std::uint64_t durationMs{}, remainingMs{}, anchorMs{}, clockMs{};
    bool running{}, valid{};
};
constexpr NativeCountdownPublication native_completion_countdown(std::uint64_t matchDuration,
    std::uint64_t matchElapsed, std::uint64_t endedAt, std::uint64_t deliveredAt,
    std::uint64_t now, std::uint8_t phase) noexcept {
    NativeCountdownPublication value{};
    const auto afterEnd = now >= endedAt ? now-endedAt : 0;
    const auto beforeDelivery = deliveredAt >= endedAt ? deliveredAt-endedAt : 0;
    if (!matchDuration || matchElapsed > UINT64_MAX-afterEnd
        || matchElapsed > UINT64_MAX-beforeDelivery) return value;
    value.clockMs = matchElapsed+afterEnd;
    value.anchorMs = matchElapsed+beforeDelivery;
    if (phase==6 || phase==7) {
        value.durationMs = completion::duration(phase);
        value.remainingMs = value.durationMs;
        value.running = true;
        if (phase==7) {
            if (value.anchorMs > UINT64_MAX-completion::kMatchEndMs) return {};
            value.anchorMs += completion::kMatchEndMs;
        }
    } else {
        value.durationMs = matchDuration;
        value.anchorMs = value.clockMs;
    }
    value.valid = true;
    return value;
}
}
