#pragma once

#include <cstdint>

namespace dawn::core::ui::hud {

enum class Overlay : std::uint8_t {
    logoCard,
    currentStatus,
    session,
    count,
};

enum class StatusLine : std::uint8_t {
    activity,
    bubble,
    sliceSet,
    closestSpawn,
    count,
};

// runs at boot before the first frame, the only point another thread touches the switches
void initialize(void* module) noexcept;

void shutdown() noexcept;

[[nodiscard]] const char* display_name(Overlay overlay) noexcept;

[[nodiscard]] bool enabled(Overlay overlay) noexcept;

void set_enabled(Overlay overlay, bool on) noexcept;

[[nodiscard]] const char* display_name(StatusLine line) noexcept;

[[nodiscard]] bool enabled(StatusLine line) noexcept;

void set_enabled(StatusLine line, bool on) noexcept;

[[nodiscard]] bool draw(bool interfaceEnabled) noexcept;

} // namespace dawn::core::ui::hud
