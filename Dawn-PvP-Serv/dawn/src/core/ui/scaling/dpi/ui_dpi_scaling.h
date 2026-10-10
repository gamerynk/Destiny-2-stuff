#pragma once

#include <Windows.h>

#include <imgui.h>

namespace dawn::core::ui::scaling::dpi {

struct Value {
    UINT effectiveDpi{USER_DEFAULT_SCREEN_DPI};
    UINT viewportWidth{};
    UINT viewportHeight{};
    float scale{1.0F};
};

[[nodiscard]] bool
calculate(UINT reportedDpi, UINT viewportWidth, UINT viewportHeight, Value& output) noexcept;

[[nodiscard]] bool update(HWND window) noexcept;

[[nodiscard]] float current() noexcept;

[[nodiscard]] float pixels(float authoredPixels) noexcept;

[[nodiscard]] ImVec2 pixels(const ImVec2& authoredPixels) noexcept;

void reset() noexcept;

} // namespace dawn::core::ui::scaling::dpi
