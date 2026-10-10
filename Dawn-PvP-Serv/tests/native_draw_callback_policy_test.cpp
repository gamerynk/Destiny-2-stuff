#include <cassert>
#include <cstdio>
#include <imgui_internal.h>
#include "../dawn/src/client/hooks/graphics/renderer/native_draw_callback_policy.h"
namespace { void foreign_callback(const ImDrawList*, const ImDrawCmd*) {} }
int main() {
    using dawn::client::hooks::graphics::renderer::native_draw_callbacks_supported;
    ImGui::CreateContext();
    {
        ImDrawList commands{ImGui::GetDrawListSharedData()};
        commands._ResetForNewFrame();
        ImDrawData data;
        data.CmdLists.push_back(&commands); data.CmdListsCount = 1;
        assert(native_draw_callbacks_supported(nullptr));
        assert(native_draw_callbacks_supported(&data));
        commands.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        assert(native_draw_callbacks_supported(&data));
        commands.AddCallback(&foreign_callback, nullptr);
        assert(!native_draw_callbacks_supported(&data));
    }
    ImGui::DestroyContext();
    std::puts("PASS: actual ImGui command lists accept ordinary/reset commands and reject foreign callbacks before native submission.");
}
