#pragma once
#include <cstdint>

namespace dawn::state::activity::crucible::completion {
inline constexpr std::uint64_t kMatchEndMs = 5'000;
inline constexpr std::uint64_t kResultsMs = 30'000;
constexpr std::uint8_t phase(std::uint64_t now, std::uint64_t deliveredAt) noexcept {
    const auto age = now >= deliveredAt ? now - deliveredAt : 0;
    return age < kMatchEndMs ? 6 : age < kMatchEndMs + kResultsMs ? 7 : 8;
}
constexpr std::uint64_t duration(std::uint8_t nativePhase) noexcept {
    return nativePhase == 6 ? kMatchEndMs : nativePhase == 7 ? kResultsMs : 0;
}
}
