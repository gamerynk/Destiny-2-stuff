#include "ui_dpi_scaling.h"

#include <Windows.h>

#include <algorithm>
#include <imgui.h>

namespace dawn::core::ui::scaling::dpi {
namespace {

constexpr UINT kAuthoredDpi = USER_DEFAULT_SCREEN_DPI;
constexpr UINT kAuthoredViewportWidth = 1920;
constexpr UINT kAuthoredViewportHeight = 1080;
constexpr UINT kMinimumDpi = 72;
constexpr UINT kMaximumDpi = 240;
constexpr UINT kInvalidDpi = 0;
constexpr UINT kInvalidViewportExtent = 0;
constexpr float kAuthoredComfortScale = 1.125F;
constexpr float kMinimumViewportScale = 0.8F;
constexpr float kMaximumViewportScale = 1.75F;
constexpr float kMinimumScale = kMinimumViewportScale * kAuthoredComfortScale;
constexpr float kMaximumScale = 2.5F;

Value g_current{};

} // namespace

bool calculate(UINT reportedDpi, UINT viewportWidth, UINT viewportHeight, Value& output) noexcept {
    if (reportedDpi == kInvalidDpi || viewportWidth == kInvalidViewportExtent
        || viewportHeight == kInvalidViewportExtent) {
        return false;
    }
    const UINT effectiveDpi = (std::clamp)(reportedDpi, kMinimumDpi, kMaximumDpi);
    const float dpiScale = static_cast<float>(effectiveDpi) / static_cast<float>(kAuthoredDpi);
    const float widthScale =
        static_cast<float>(viewportWidth) / static_cast<float>(kAuthoredViewportWidth);
    const float heightScale =
        static_cast<float>(viewportHeight) / static_cast<float>(kAuthoredViewportHeight);
    const float viewportScale = (std::clamp)((std::min)(widthScale, heightScale),
                                             kMinimumViewportScale,
                                             kMaximumViewportScale);
    // at base DPI the UI may shrink; above it, DPI and framebuffer scale don't stack
    const float environmentalScale =
        dpiScale <= 1.0F ? viewportScale : (std::max)(viewportScale, dpiScale);
    const float scale =
        (std::clamp)(environmentalScale * kAuthoredComfortScale, kMinimumScale, kMaximumScale);
    output = {effectiveDpi, viewportWidth, viewportHeight, scale};
    return true;
}

bool update(HWND window) noexcept {
    if (window == nullptr) {
        return false;
    }
    const UINT reportedDpi = GetDpiForWindow(window);
    RECT clientArea{};
    if (GetClientRect(window, &clientArea) == FALSE || clientArea.right <= clientArea.left
        || clientArea.bottom <= clientArea.top) {
        return false;
    }
    const UINT viewportWidth = static_cast<UINT>(clientArea.right - clientArea.left);
    const UINT viewportHeight = static_cast<UINT>(clientArea.bottom - clientArea.top);
    Value calculated{};
    if (!calculate(reportedDpi, viewportWidth, viewportHeight, calculated)) {
        return false;
    }
    if (calculated.effectiveDpi == g_current.effectiveDpi
        && calculated.viewportWidth == g_current.viewportWidth
        && calculated.viewportHeight == g_current.viewportHeight) {
        return false;
    }
    const bool scaleChanged = calculated.scale != g_current.scale;
    g_current = calculated;
    return scaleChanged;
}

float current() noexcept {
    return g_current.scale;
}

float pixels(float authoredPixels) noexcept {
    return authoredPixels * current();
}

ImVec2 pixels(const ImVec2& authoredPixels) noexcept {
    const float scale = current();
    return {authoredPixels.x * scale, authoredPixels.y * scale};
}

void reset() noexcept {
    g_current = {};
}

} // namespace dawn::core::ui::scaling::dpi
