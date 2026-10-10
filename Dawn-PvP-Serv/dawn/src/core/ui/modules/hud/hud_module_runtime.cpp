#include <string_view>

#include "../../hud/overlay.h"
#include "../registry/ui_module_registry.h"
#include "../ui_module_descriptor.h"
#include "hud.h"
#include "internal.h"

namespace dawn::core::ui::modules::hud {
namespace {

constexpr std::string_view kStableId = "core.hud";
constexpr std::string_view kDisplayName = "HUD";

registry::PageRegistration g_page;

} // namespace

bool initialize(void* module) noexcept {
    // load before the page registers so the first frame draws the saved switches
    ui::hud::initialize(module);
    return g_page.acquire(Owner::core, kStableId, kDisplayName, &internal::draw);
}

void shutdown() noexcept {
    g_page.release();
    ui::hud::shutdown();
}

} // namespace dawn::core::ui::modules::hud
