#pragma once

#include <algorithm>
#include <imgui.h>

namespace dawn::core::ui::components::drawing {

[[nodiscard]] inline float normalized(float value) noexcept {
    return (std::clamp)(value, 0.0F, 1.0F);
}

[[nodiscard]] inline ImVec4 blend(const ImVec4& from, const ImVec4& to, float weight) noexcept {
    const float amount = normalized(weight);
    return {from.x + ((to.x - from.x) * amount),
            from.y + ((to.y - from.y) * amount),
            from.z + ((to.z - from.z) * amount),
            from.w + ((to.w - from.w) * amount)};
}

[[nodiscard]] inline const char* visible_text_end(const char* label) noexcept {
    if (label == nullptr) {
        return nullptr;
    }
    const char* cursor = label;
    while (*cursor != '\0' && !(cursor[0] == '#' && cursor[1] == '#')) {
        ++cursor;
    }
    return cursor;
}

} // namespace dawn::core::ui::components::drawing
