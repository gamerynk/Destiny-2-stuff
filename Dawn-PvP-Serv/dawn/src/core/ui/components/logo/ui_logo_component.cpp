#include "ui_logo_component.h"

#include <cmath>
#include <imgui.h>

#include "../../textures/ui_texture_slots.h"

namespace dawn::core::ui::components::logo {
namespace {

constexpr float kSheetExtent = 1408.0F;
constexpr float kFrameExtent = 128.0F;
constexpr int kFrameColumns = 11;
// the sheet is 11x11 cells but the animation uses only the first 112
constexpr int kFrameCount = 112;
constexpr double kFrameSeconds = 0.033;
constexpr ImU32 kGradientTop = IM_COL32(0xCB, 0x9B, 0x51, 0xFF);
constexpr ImU32 kGradientBottom = IM_COL32(0xF6, 0xE2, 0x7A, 0xFF);
constexpr int kQuadIndexCount = 6;
constexpr int kQuadVertexCount = 4;
constexpr ImDrawIdx kTopRightOffset = 1;
constexpr ImDrawIdx kBottomRightOffset = 2;
constexpr ImDrawIdx kBottomLeftOffset = 3;

[[nodiscard]] int current_frame() noexcept {
    const double elapsed = ImGui::GetTime() / kFrameSeconds;
    return static_cast<int>(std::fmod(elapsed, static_cast<double>(kFrameCount)));
}

void draw_gradient_quad(ImTextureID sheet,
                        const ImVec2& minimum,
                        const ImVec2& maximum,
                        const ImVec2& uvMinimum,
                        const ImVec2& uvMaximum) noexcept {
    const ImU32 top = ImGui::GetColorU32(kGradientTop);
    const ImU32 bottom = ImGui::GetColorU32(kGradientBottom);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushTexture(ImTextureRef(sheet));
    drawList->PrimReserve(kQuadIndexCount, kQuadVertexCount);
    const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kTopRightOffset));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomRightOffset));
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomRightOffset));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomLeftOffset));
    drawList->PrimWriteVtx(minimum, uvMinimum, top);
    drawList->PrimWriteVtx({maximum.x, minimum.y}, {uvMaximum.x, uvMinimum.y}, top);
    drawList->PrimWriteVtx(maximum, uvMaximum, bottom);
    drawList->PrimWriteVtx({minimum.x, maximum.y}, {uvMinimum.x, uvMaximum.y}, bottom);
    drawList->PopTexture();
}

} // namespace

bool draw(float extent) noexcept {
    const ImTextureID sheet = textures::get(textures::Slot::logoSheet);
    if (sheet == ImTextureID_Invalid) {
        return false;
    }
    const int frame = current_frame();
    const float step = kFrameExtent / kSheetExtent;
    const float column = static_cast<float>(frame % kFrameColumns) * step;
    const float row = static_cast<float>(frame / kFrameColumns) * step;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    draw_gradient_quad(sheet,
                       origin,
                       {origin.x + extent, origin.y + extent},
                       {column, row},
                       {column + step, row + step});
    ImGui::Dummy({extent, extent});
    return true;
}

} // namespace dawn::core::ui::components::logo
