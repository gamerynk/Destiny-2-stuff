#pragma once

#include <cstdint>
#include <imgui.h>

namespace dawn::core::ui::animation::transition {

enum class Lane : std::uint8_t {
    hover,
    selection,
    focus,
    visibility,
};

struct Rates {
    float risePerSecond{};
    float fallPerSecond{};
};

[[nodiscard]] float
update(ImGuiID id, Lane lane, bool enabled, Rates rates, float initialValue) noexcept;

void reset() noexcept;

} // namespace dawn::core::ui::animation::transition
