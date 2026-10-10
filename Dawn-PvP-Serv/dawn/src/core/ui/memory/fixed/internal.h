#pragma once

#include <cstddef>

namespace dawn::core::ui::memory::fixed {

struct ArenaStats {
    std::size_t outstandingAllocations{};
    std::size_t outstandingBytes{};
    std::size_t highWaterBytes{};
    std::size_t largestFreeBytes{};
};

void reset() noexcept;

void clear() noexcept;

[[nodiscard]] void* allocate(std::size_t requestedBytes) noexcept;

void release(void* pointer) noexcept;

[[nodiscard]] ArenaStats snapshot() noexcept;

} // namespace dawn::core::ui::memory::fixed
