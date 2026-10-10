#include <cstddef>
#include <imgui.h>

#include "../../components/section/ui_section_component.h"
#include "../../components/toggle/ui_toggle_component.h"
#include "../../hud/overlay.h"
#include "internal.h"

namespace dawn::core::ui::modules::hud::internal {

void draw() noexcept {
    ImGui::TextWrapped("Overlays draw in the top-left corner while the game runs, with or "
                       "without this menu open.");
    ImGui::Spacing();

    for (std::size_t index = 0; index < static_cast<std::size_t>(ui::hud::Overlay::count);
         ++index) {
        const auto overlay = static_cast<ui::hud::Overlay>(index);
        bool on = ui::hud::enabled(overlay);
        if (components::toggle::control(ui::hud::display_name(overlay), on)) {
            ui::hud::set_enabled(overlay, on);
        }
    }

    ImGui::Spacing();
    components::section::header("Current status lines",
                                "Each line of the current status overlay, on its own.");
    ImGui::Spacing();
    for (std::size_t index = 0; index < static_cast<std::size_t>(ui::hud::StatusLine::count);
         ++index) {
        const auto line = static_cast<ui::hud::StatusLine>(index);
        bool on = ui::hud::enabled(line);
        if (components::toggle::control(ui::hud::display_name(line), on)) {
            ui::hud::set_enabled(line, on);
        }
    }
}

} // namespace dawn::core::ui::modules::hud::internal
