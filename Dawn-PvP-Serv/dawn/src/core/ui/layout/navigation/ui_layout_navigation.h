#pragma once

#include "../../modules/ui_module_descriptor.h"
#include "../layout.h"

namespace dawn::core::ui::layout::navigation {

struct Selection {
    modules::Descriptor descriptor;
    bool moduleAvailable{};
};

[[nodiscard]] Selection draw(const StateSnapshot& state) noexcept;

} // namespace dawn::core::ui::layout::navigation
