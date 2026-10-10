#pragma once

#include <Windows.h>

#include <cstdint>

#include "settings.h"

namespace dawn::core::ui::runtime {

struct VisibilitySnapshot {
    bool initialized{};
    bool enabled{};
    bool visible{};
    UINT toggleVirtualKey{VK_INSERT};
    std::uint64_t generation{};
};

[[nodiscard]] bool initialize(const Settings& settings) noexcept;

void shutdown() noexcept;

[[nodiscard]] VisibilitySnapshot snapshot() noexcept;

[[nodiscard]] bool toggle_for_key(UINT virtualKey) noexcept;

// callers never hold this lock across native input, raw-window or Win32 operations
[[nodiscard]] bool open_for_key(UINT virtualKey, std::uint64_t hiddenGeneration,
                                VisibilitySnapshot& opened) noexcept;
[[nodiscard]] bool close_generation(std::uint64_t visibleGeneration,
                                   VisibilitySnapshot& closed) noexcept;

} // namespace dawn::core::ui::runtime
