#pragma once

namespace dawn::core::ui::components::navigation {

enum class LabelAlignment {
    leading,
    centered,
};

[[nodiscard]] bool tab(const char* label,
                       bool selected,
                       float width = 0.0F,
                       float height = 0.0F,
                       LabelAlignment alignment = LabelAlignment::leading) noexcept;

} // namespace dawn::core::ui::components::navigation
