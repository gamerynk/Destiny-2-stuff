#include <cassert>
#include <cstdio>
#include "core/ui/runtime/ui_visibility_runtime.h"
namespace ui = dawn::core::ui::runtime;
int main() {
    ui::Settings settings{};
    settings.enabled = true;
    settings.toggleVirtualKey = VK_INSERT;
    assert(ui::initialize(settings));
    auto state = ui::snapshot();
    assert(state.initialized && !state.visible && state.generation != 0);
#if defined(RESTORATION_NATIVE_OVERLAY_OMISSION) && RESTORATION_NATIVE_OVERLAY_OMISSION
    assert(!state.enabled);
    assert(!ui::toggle_for_key(VK_INSERT));
    ui::VisibilitySnapshot opened{};
    assert(!ui::open_for_key(VK_INSERT, state.generation, opened));
    assert(!ui::snapshot().visible);
#else
    assert(state.enabled);
    assert(ui::toggle_for_key(VK_INSERT));
    assert(ui::snapshot().visible);
#endif
    ui::shutdown();
    assert(!ui::snapshot().initialized);
    assert(ui::initialize(settings));
#if defined(RESTORATION_NATIVE_OVERLAY_OMISSION) && RESTORATION_NATIVE_OVERLAY_OMISSION
    assert(!ui::snapshot().enabled);
#endif
    ui::shutdown();
    settings.enabled = false;
    assert(ui::initialize(settings));
    state = ui::snapshot();
    assert(state.initialized && !state.enabled && !state.visible);
    assert(!ui::toggle_for_key(VK_INSERT));
    ui::VisibilitySnapshot disabledOpened{};
    assert(!ui::open_for_key(VK_INSERT, state.generation, disabledOpened));
    assert(!ui::snapshot().visible);
    ui::shutdown();
    std::puts("PASS: overlay profile survives enabled settings, toggle/open and reinitialization");
}
