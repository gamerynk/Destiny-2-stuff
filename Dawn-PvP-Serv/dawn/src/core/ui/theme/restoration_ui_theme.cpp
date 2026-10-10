#include "restoration_ui_theme.h"

#include <imgui.h>

#include "../scaling/dpi/ui_dpi_scaling.h"

namespace dawn::core::ui::theme {
namespace {

constexpr float kWindowRounding = 8.0F;
constexpr float kControlRounding = 6.0F;
constexpr float kBorderWidth = 1.0F;
constexpr float kNoFrameBorderWidth = 0.0F;
constexpr ImVec2 kWindowPadding{12.0F, 10.0F};
constexpr ImVec2 kItemSpacing{10.0F, 6.0F};
constexpr ImVec2 kFramePadding{7.0F, 4.0F};

constexpr ImVec4 kText{0.92F, 0.94F, 0.97F, 1.0F};
constexpr ImVec4 kMutedText{0.48F, 0.53F, 0.61F, 1.0F};
constexpr ImVec4 kWindow{0.035F, 0.043F, 0.058F, 0.98F};
constexpr ImVec4 kPanel{0.055F, 0.066F, 0.087F, 1.0F};
constexpr ImVec4 kControl{0.095F, 0.11F, 0.14F, 1.0F};
constexpr ImVec4 kControlHovered{0.14F, 0.16F, 0.20F, 1.0F};
constexpr ImVec4 kAccent{0.20F, 0.55F, 0.90F, 1.0F};
constexpr ImVec4 kAccentHovered{0.30F, 0.65F, 1.0F, 1.0F};
constexpr ImVec4 kAccentActive{0.14F, 0.42F, 0.75F, 1.0F};
constexpr ImVec4 kBorder{0.16F, 0.19F, 0.24F, 1.0F};
constexpr ImVec4 kSelection{0.20F, 0.55F, 0.90F, 0.28F};
constexpr ImVec4 kTransparent{};

} // namespace

void apply() noexcept {
    const float fontSizeBase = ImGui::GetStyle().FontSizeBase;
    ImGuiStyle style{};
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = kWindowPadding;
    style.FramePadding = kFramePadding;
    style.ItemSpacing = kItemSpacing;
    style.WindowRounding = kWindowRounding;
    style.ChildRounding = kControlRounding;
    style.PopupRounding = kControlRounding;
    style.FrameRounding = kControlRounding;
    style.GrabRounding = kControlRounding;
    style.ScrollbarRounding = kControlRounding;
    style.WindowBorderSize = kBorderWidth;
    style.ChildBorderSize = kBorderWidth;
    style.PopupBorderSize = kBorderWidth;
    style.FrameBorderSize = kNoFrameBorderWidth;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kMutedText;
    colors[ImGuiCol_WindowBg] = kWindow;
    colors[ImGuiCol_ChildBg] = kPanel;
    colors[ImGuiCol_PopupBg] = kPanel;
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = kTransparent;
    colors[ImGuiCol_FrameBg] = kControl;
    colors[ImGuiCol_FrameBgHovered] = kControlHovered;
    colors[ImGuiCol_FrameBgActive] = kAccentActive;
    colors[ImGuiCol_TitleBg] = kWindow;
    colors[ImGuiCol_TitleBgActive] = kWindow;
    colors[ImGuiCol_TitleBgCollapsed] = kWindow;
    colors[ImGuiCol_ScrollbarBg] = kPanel;
    colors[ImGuiCol_ScrollbarGrab] = kControl;
    colors[ImGuiCol_ScrollbarGrabHovered] = kControlHovered;
    colors[ImGuiCol_ScrollbarGrabActive] = kAccentActive;
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentHovered;
    colors[ImGuiCol_Button] = kControl;
    colors[ImGuiCol_ButtonHovered] = kControlHovered;
    colors[ImGuiCol_ButtonActive] = kAccentActive;
    colors[ImGuiCol_Header] = kSelection;
    colors[ImGuiCol_HeaderHovered] = kControlHovered;
    colors[ImGuiCol_HeaderActive] = kAccentActive;
    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccent;
    colors[ImGuiCol_SeparatorActive] = kAccentHovered;
    colors[ImGuiCol_ResizeGrip] = kSelection;
    colors[ImGuiCol_ResizeGripHovered] = kAccent;
    colors[ImGuiCol_ResizeGripActive] = kAccentHovered;
    colors[ImGuiCol_TextSelectedBg] = kSelection;
    colors[ImGuiCol_NavCursor] = kAccent;

    // scaling a fresh default style stops repeated monitor changes from accumulating error
    const float scale = scaling::dpi::current();
    style.ScaleAllSizes(scale);
    style.FontSizeBase = fontSizeBase;
    style.FontScaleMain = scale;
    ImGui::GetStyle() = style;
}

} // namespace dawn::core::ui::theme
