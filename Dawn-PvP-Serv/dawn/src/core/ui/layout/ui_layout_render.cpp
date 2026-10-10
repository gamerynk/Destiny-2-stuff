#include <algorithm>
#include <array>
#include <imgui.h>
#include <string_view>

#include "../../../../resources/resource.h"
#include "../animation/transition/ui_transition_animation.h"
#include "../components/card/ui_card_component.h"
#include "../components/section/ui_section_component.h"
#include "../scaling/dpi/ui_dpi_scaling.h"
#include "navigation/ui_layout_navigation.h"
#include "ui_layout_lifecycle.h"

namespace dawn::core::ui::layout {
namespace {

constexpr float kPreferredWindowWidth = 920.0F;
constexpr float kPreferredWindowHeight = 580.0F;
constexpr float kMinimumWindowWidth = 420.0F;
constexpr float kMinimumWindowHeight = 300.0F;
constexpr float kViewportMargin = 24.0F;
constexpr float kViewportMarginCount = 2.0F;
constexpr float kNavigationWidth = 180.0F;
constexpr float kAutomaticWidth = 0.0F;
constexpr ImVec2 kCenterPivot{0.5F, 0.5F};
constexpr ImGuiWindowFlags kMainWindowFlags =
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoTitleBar;
constexpr std::size_t kLabelTerminatorBytes = 1;
// every visibility-lane user needs its own key, so keep these distinct
constexpr ImGuiID kSurfaceAnimationId = 1;
constexpr animation::transition::Rates kVisibilityRates{16.0F, 14.0F};
constexpr float kClosedProgress = 0.0F;
constexpr float kOpeningScale = 0.96F;
constexpr float kOpenScale = 1.0F;
constexpr float kTitleTextRatio = 1.5F;
constexpr char kTitle[] = "RESTORATION";

[[nodiscard]] std::array<char, modules::kDisplayNameCapacity + kLabelTerminatorBytes>
component_label(const modules::Descriptor& descriptor) noexcept {
    std::array<char, modules::kDisplayNameCapacity + kLabelTerminatorBytes> label{};
    const std::string_view displayName = descriptor.display_name();
    std::copy(displayName.begin(), displayName.end(), label.begin());
    return label;
}

[[nodiscard]] ImVec2 window_size(const ImGuiViewport& viewport) noexcept {
    if (viewport.Size.x <= 0.0F || viewport.Size.y <= 0.0F) {
        return {};
    }
    const float margin = scaling::dpi::pixels(kViewportMargin);
    const float availableWidth = viewport.Size.x - (margin * kViewportMarginCount);
    const float availableHeight = viewport.Size.y - (margin * kViewportMarginCount);
    const float minimumWidth = scaling::dpi::pixels(kMinimumWindowWidth);
    const float minimumHeight = scaling::dpi::pixels(kMinimumWindowHeight);
    if (availableWidth < minimumWidth || availableHeight < minimumHeight) {
        return {};
    }
    return {(std::min)(scaling::dpi::pixels(kPreferredWindowWidth), availableWidth),
            (std::min)(scaling::dpi::pixels(kPreferredWindowHeight), availableHeight)};
}

void draw_content(const navigation::Selection& selected) noexcept {
    if (!selected.moduleAvailable) {
        ImGui::TextDisabled("No modules are registered.");
        return;
    }
    const auto displayName = component_label(selected.descriptor);
    components::section::header(displayName.data());
    ImGui::Dummy({kAutomaticWidth, ImGui::GetStyle().ItemSpacing.y});
    selected.descriptor.frame_callback()();
}

void draw_title() noexcept {
    // authored size, because the style carries the display scale separately
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kTitleTextRatio);
    ImGui::TextUnformatted(kTitle);
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::TextDisabled(RESTORATION_VER_STRING);
}

} // namespace

bool render(bool visible) noexcept {
    if (!internal::context_is_current()) {
        return false;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return false;
    }
    const ImVec2 size = window_size(*viewport);
    if (size.x <= 0.0F || size.y <= 0.0F) {
        return false;
    }
    const float progress = animation::transition::update(kSurfaceAnimationId,
                                                         animation::transition::Lane::visibility,
                                                         visible,
                                                         kVisibilityRates,
                                                         kClosedProgress);
    if (progress <= kClosedProgress) {
        return false;
    }

    const float scale = kOpeningScale + ((kOpenScale - kOpeningScale) * progress);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, kCenterPivot);
    ImGui::SetNextWindowSize({size.x * scale, size.y * scale}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, progress);
    const bool submitContents = ImGui::Begin("Restoration", nullptr, kMainWindowFlags);
    if (submitContents) {
        draw_title();
        ImGui::Separator();

        const StateSnapshot state = snapshot();
        navigation::Selection selected{};
        const float panelHeight = ImGui::GetContentRegionAvail().y;
        {
            const components::card::Scope navigationCard(
                "##navigation_card", ImVec2(scaling::dpi::pixels(kNavigationWidth), panelHeight));
            if (navigationCard.visible()) {
                selected = navigation::draw(state);
            }
        }
        ImGui::SameLine();
        {
            const components::card::Scope contentCard("##content_card",
                                                      ImVec2(kAutomaticWidth, panelHeight));
            if (contentCard.visible()) {
                draw_content(selected);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return true;
}

} // namespace dawn::core::ui::layout
