#include "native_draw_callback_policy.h"
#include <Windows.h>

#include <array>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "../../../../core/ui/fonts/runtime/ui_runtime_font_lifecycle.h"
#include "../../../../core/ui/hud/overlay.h"
#include "../../../../core/ui/layout/layout.h"
#include "../../../../core/ui/notice/ui_notice_overlay.h"
#include "../../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../../../core/ui/theme/restoration_ui_theme.h"
#include "../input/input.h"
#include "graphics_renderer_report.h"
#include "state.h"
#include "../../../crucible/match_panel.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window,
                                                             UINT message,
                                                             WPARAM word,
                                                             LPARAM value);

namespace dawn::client::hooks::graphics::renderer {
namespace {

constexpr UINT kOverlayRenderTargetCount = 1;
// a visible UI eats this whole range; stopping short of the wheel or extra buttons
// would leak them to the game
static_assert(WM_MOUSEWHEEL <= WM_MOUSELAST);
static_assert(WM_MOUSEHWHEEL <= WM_MOUSELAST);
static_assert(WM_XBUTTONDBLCLK <= WM_MOUSELAST);

[[nodiscard]] bool is_mouse_input(UINT message) noexcept {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

[[nodiscard]] bool is_raw_input(UINT message) noexcept {
    return message == WM_INPUT;
}

[[nodiscard]] bool is_keyboard_input(UINT message) noexcept {
    switch (message) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        return true;
    default:
        return false;
    }
}

void transition_input_visibility_locked(bool visible) noexcept {
    ImGuiIO& io = ImGui::GetIO();
    // the game draws a virtual cursor, so visible frames need ImGui's software cursor
    io.MouseDrawCursor = visible;
    if (visible) {
        g_resources.inputVisible = true;
        return;
    }
    if (!g_resources.inputVisible) {
        return;
    }

    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
    g_resources.inputVisible = false;
    // deferred because ReleaseCapture can re-enter WndProc at once
    g_captureReleaseWindow = g_resources.window;
}

void draw_data(ImDrawData* drawData) noexcept {
    if (drawData == nullptr) {
        return;
    }
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> priorTargets{};
    ID3D11DepthStencilView* priorDepth = nullptr;
    g_resources.context->OMGetRenderTargets(
        static_cast<UINT>(priorTargets.size()), priorTargets.data(), &priorDepth);

    ID3D11RenderTargetView* overlayTarget = g_resources.renderTarget;
    g_resources.context->OMSetRenderTargets(kOverlayRenderTargetCount, &overlayTarget, nullptr);
    ImGui_ImplDX11_RenderDrawData(drawData);
    g_resources.context->OMSetRenderTargets(
        static_cast<UINT>(priorTargets.size()), priorTargets.data(), priorDepth);

    for (ID3D11RenderTargetView* target : priorTargets) {
        if (target != nullptr) {
            target->Release();
        }
    }
    if (priorDepth != nullptr) {
        priorDepth->Release();
    }
}

} // namespace

bool prepare_frame_locked() noexcept {
    if (!fully_active_locked()) {
        return false;
    }
    if (core::ui::scaling::dpi::update(g_resources.window)) {
        core::ui::theme::apply();
        if (!core::ui::fonts::runtime::apply_scale(core::ui::scaling::dpi::current())) {
            report::note(report::Stage::frame, report::Reason::fontScale);
            (void)shutdown_locked();
            return false;
        }
    }
    const core::ui::runtime::VisibilitySnapshot visibility = core::ui::runtime::snapshot();
    transition_input_visibility_locked(visibility.visible);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    // a hidden surface still draws until its close animation ends, so layout decides; HUD
    // draws first so the surface stays above it
    const bool hudDrawn = core::ui::hud::draw(visibility.enabled);
    const bool surfaceDrawn = core::ui::layout::render(visibility.visible);
    const bool noticeDrawn = core::ui::notice::draw();
    const bool matchDrawn = client::crucible::draw();
    if (!hudDrawn && !surfaceDrawn && !noticeDrawn && !matchDrawn) {
        ImGui::EndFrame();
        return false;
    }
    ImGui::Render();
    return true;
}

void draw_prepared_data_locked() noexcept {
    draw_data(ImGui::GetDrawData());
}

bool native_draw_callbacks_supported_locked() noexcept {
    return native_draw_callbacks_supported(ImGui::GetDrawData());
}

void render_frame_locked() noexcept {
    if (prepare_frame_locked()) draw_prepared_data_locked();
}

bool handle_preparation_message(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    if (!fully_active_locked() || g_resources.window != window) {
        return false;
    }

    const core::ui::runtime::VisibilitySnapshot visibility = core::ui::runtime::snapshot();
    transition_input_visibility_locked(visibility.visible);
    if (!visibility.visible) {
        return false;
    }

    (void)ImGui_ImplWin32_WndProcHandler(window, message, word, value);
    // a visible UI is modal: no mouse, keyboard or raw input reaches the game
    const bool capture =
        is_mouse_input(message) || is_keyboard_input(message) || is_raw_input(message);
    return capture;
}

bool handle_window_message(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    const bool captured = handle_preparation_message(window, message, word, value);
    ReleaseSRWLockExclusive(&g_rendererLock);
    return captured;
}

void dispatch_pending_input_release(HWND window) noexcept {
    AcquireSRWLockExclusive(&g_rendererLock);
    const DWORD windowThread = GetWindowThreadProcessId(window, nullptr);
    const bool releaseCapture = g_captureReleaseWindow == window && windowThread != 0
                                && windowThread == GetCurrentThreadId();
    if (releaseCapture) {
        g_captureReleaseWindow = nullptr;
    }
    ReleaseSRWLockExclusive(&g_rendererLock);

    if (releaseCapture && GetCapture() == window) {
        // drop the renderer lock first; Windows sends WM_CAPTURECHANGED at once
        (void)ReleaseCapture();
    }
}

void dispatch_pending_input_release() noexcept {
    AcquireSRWLockShared(&g_rendererLock);
    const HWND window = g_captureReleaseWindow;
    ReleaseSRWLockShared(&g_rendererLock);

    if (window == nullptr) {
        return;
    }
    const DWORD windowThread = GetWindowThreadProcessId(window, nullptr);
    if (windowThread != 0 && windowThread != GetCurrentThreadId()) {
        // the game's presentation message still reaches hooked Present even if we were unhooked
        (void)PostMessageW(window, input::kRequiredForwardMessage, 0, 0);
        return;
    }
    dispatch_pending_input_release(window);
}

} // namespace dawn::client::hooks::graphics::renderer
