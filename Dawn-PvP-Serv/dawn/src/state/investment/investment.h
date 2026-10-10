#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state {

inline constexpr std::size_t kUnlockOverrideCapacity = 100;

struct UnlockFlagOverride {
    std::uint16_t slot{};
    std::uint8_t value{};
};

struct UnlockValueOverride {
    std::uint16_t slot{};
    std::int32_t value{};
};

struct Family5State {
    std::uint64_t objectSoid{};
    std::array<UnlockFlagOverride, kUnlockOverrideCapacity> flags{};
    std::size_t flagCount{};
    std::array<UnlockValueOverride, kUnlockOverrideCapacity> values{};
    std::size_t valueCount{};
    bool contentGateArm{};
};

struct InvestmentState {
    Family5State family5;
};

} // namespace dawn::state
