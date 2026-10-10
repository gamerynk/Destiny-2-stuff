#pragma once

#include <cstddef>

namespace dawn::core::ui::memory {

inline constexpr std::size_t kArenaCapacityBytes = 8'388'608;

struct Stats {
    bool installed{};
    std::size_t capacityBytes{kArenaCapacityBytes};
    std::size_t outstandingAllocations{};
    std::size_t outstandingBytes{};
    std::size_t highWaterBytes{};
    std::size_t largestFreeBytes{};
};

[[nodiscard]] bool initialize() noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] Stats snapshot() noexcept;

} // namespace dawn::core::ui::memory
