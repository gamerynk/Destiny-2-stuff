#pragma once

#include <imgui.h>

namespace dawn::client::hooks::graphics::renderer {
// native ownership permits the backend reset sentinel, but no arbitrary application callback
[[nodiscard]] inline bool native_draw_callbacks_supported(const ImDrawData* data) noexcept {
    if (!data) return true;
    for (int list = 0; list < data->CmdListsCount; ++list) {
        for (const ImDrawCmd& command : data->CmdLists[list]->CmdBuffer) {
            if (command.UserCallback && command.UserCallback != ImDrawCallback_ResetRenderState)
                return false;
        }
    }
    return true;
}
}
