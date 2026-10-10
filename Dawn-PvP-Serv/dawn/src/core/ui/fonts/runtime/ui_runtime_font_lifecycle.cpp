#include "ui_runtime_font_lifecycle.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <imgui.h>

#include "../installed/ui_installed_font_reader.h"

namespace dawn::core::ui::fonts::runtime {
namespace {

constexpr float kMinimumBasePixelSize = 8.0F;
constexpr float kMaximumBasePixelSize = 64.0F;
constexpr float kMinimumDpiScale = 0.75F;
constexpr float kMaximumDpiScale = 2.5F;
constexpr float kAuthoredScale = 1.0F;
constexpr float kRasterizerDensity = 2.0F;

struct State {
    bool initialized{};
    Source source{Source::unavailable};
    ImGuiContext* context{};
    ImFontAtlas* atlas{};
    float priorBasePixelSize{};
    float priorMainScale{kAuthoredScale};
    float basePixelSize{};
    float scale{kAuthoredScale};
};

State g_state{};
SRWLOCK g_fontLock{SRWLOCK_INIT};

[[nodiscard]] bool valid_base_size(float value) noexcept {
    return std::isfinite(value) && value >= kMinimumBasePixelSize && value <= kMaximumBasePixelSize;
}

[[nodiscard]] bool valid_scale(float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] ImFont* add_embedded_font(ImFontAtlas& atlas, float basePixelSize) noexcept {
    ImFontConfig config{};
    config.SizePixels = basePixelSize;
    config.RasterizerDensity = kRasterizerDensity;
    return atlas.AddFontDefaultVector(&config);
}

} // namespace

bool initialize(HMODULE module, float basePixelSize) noexcept {
    AcquireSRWLockExclusive(&g_fontLock);
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (g_state.initialized) {
        const bool active = context != nullptr && context == g_state.context;
        ReleaseSRWLockExclusive(&g_fontLock);
        return active;
    }
    if (context == nullptr || !valid_base_size(basePixelSize)) {
        ReleaseSRWLockExclusive(&g_fontLock);
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    if (atlas == nullptr || atlas->Locked) {
        ReleaseSRWLockExclusive(&g_fontLock);
        return false;
    }

    const float priorBasePixelSize = ImGui::GetStyle().FontSizeBase;
    const float priorMainScale = ImGui::GetStyle().FontScaleMain;
    atlas->Clear();
    io.FontDefault = nullptr;

    installed::DataView installedData{};
    Source source = Source::embeddedDefault;
    ImFont* font = nullptr;
    if (installed::load(module, installedData)) {
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.RasterizerDensity = kRasterizerDensity;
        font = atlas->AddFontFromMemoryTTF(
            installedData.bytes, installedData.byteCount, basePixelSize, &config);
        source = Source::installed;
    }
    if (font == nullptr) {
        atlas->Clear();
        installed::clear();
        font = add_embedded_font(*atlas, basePixelSize);
        source = Source::embeddedDefault;
    }
    if (font == nullptr) {
        atlas->Clear();
        installed::clear();
        ImGui::GetStyle().FontSizeBase = priorBasePixelSize;
        ImGui::GetStyle().FontScaleMain = priorMainScale;
        ReleaseSRWLockExclusive(&g_fontLock);
        return false;
    }

    io.FontDefault = font;
    ImGui::GetStyle().FontSizeBase = basePixelSize;
    ImGui::GetStyle().FontScaleMain = kAuthoredScale;
    g_state = {true,
               source,
               context,
               atlas,
               priorBasePixelSize,
               priorMainScale,
               basePixelSize,
               kAuthoredScale};
    ReleaseSRWLockExclusive(&g_fontLock);
    return true;
}

bool apply_scale(float scale) noexcept {
    AcquireSRWLockExclusive(&g_fontLock);
    if (!g_state.initialized || ImGui::GetCurrentContext() != g_state.context
        || !valid_scale(scale)) {
        ReleaseSRWLockExclusive(&g_fontLock);
        return false;
    }

    g_state.scale = (std::clamp)(scale, kMinimumDpiScale, kMaximumDpiScale);
    ImGui::GetStyle().FontScaleMain = g_state.scale;
    ReleaseSRWLockExclusive(&g_fontLock);
    return true;
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&g_fontLock);
    if (!g_state.initialized) {
        installed::clear();
        ReleaseSRWLockExclusive(&g_fontLock);
        return true;
    }
    if (ImGui::GetCurrentContext() != g_state.context || g_state.atlas == nullptr
        || g_state.atlas->Locked || ImGui::GetIO().Fonts != g_state.atlas) {
        ReleaseSRWLockExclusive(&g_fontLock);
        return false;
    }

    ImGui::GetIO().FontDefault = nullptr;
    g_state.atlas->Clear();
    ImGui::GetStyle().FontSizeBase = g_state.priorBasePixelSize;
    ImGui::GetStyle().FontScaleMain = g_state.priorMainScale;
    installed::clear();
    g_state = {};
    ReleaseSRWLockExclusive(&g_fontLock);
    return true;
}

Snapshot snapshot() noexcept {
    AcquireSRWLockShared(&g_fontLock);
    const Snapshot result{g_state.initialized,
                          g_state.source,
                          installed::byte_count(),
                          g_state.basePixelSize,
                          g_state.scale};
    ReleaseSRWLockShared(&g_fontLock);
    return result;
}

} // namespace dawn::core::ui::fonts::runtime
