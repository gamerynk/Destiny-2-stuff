#include "ui_hud_logo_overlay.h"

#include <imgui.h>

#include "../../../../../resources/resource.h"

namespace dawn::core::ui::hud::overlays::logo {
namespace {

constexpr char kTitle[] = "RESTORATION";

} // namespace

void draw() noexcept {
    ImGui::TextUnformatted(kTitle);
    ImGui::TextDisabled("%s", RESTORATION_VER_STRING);
}

} // namespace dawn::core::ui::hud::overlays::logo
