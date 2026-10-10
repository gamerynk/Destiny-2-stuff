#pragma once

#include <Windows.h>

#include <cstddef>
#include <d3d11.h>
#include <dxgi.h>

namespace dawn::client::hooks::graphics::renderer {

#if defined(RESTORATION_TRANSIENT_RTV) && RESTORATION_TRANSIENT_RTV
inline constexpr bool kTransientRenderTarget = true;
#else
inline constexpr bool kTransientRenderTarget = false;
#endif

struct Resources {
    IDXGISwapChain* swapChain{};
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    ID3D11RenderTargetView* renderTarget{};
    HWND window{};
    bool layoutInitialized{};
    bool win32BackendInitialized{};
    bool dx11BackendInitialized{};
    bool inputInstalled{};
    bool inputVisible{};
    bool surfaceChangeDeviceLost{};
    std::size_t activeSurfaceChanges{};
};

extern SRWLOCK g_rendererLock;
extern Resources g_resources;
[[nodiscard]] bool prepare_frame_locked() noexcept;
void draw_prepared_data_locked() noexcept;
[[nodiscard]] bool native_draw_callbacks_supported_locked() noexcept;
extern HWND g_captureReleaseWindow;

namespace selection {

[[nodiscard]] bool acquire(IDXGISwapChain* swapChain, Resources& output) noexcept;

[[nodiscard]] bool matches_output_window(IDXGISwapChain* swapChain, HWND window) noexcept;

} // namespace selection

[[nodiscard]] bool create_render_target(Resources& resources) noexcept;

void release_render_target(Resources& resources) noexcept;

void release_resources(Resources& resources) noexcept;

[[nodiscard]] bool shutdown_locked() noexcept;

[[nodiscard]] bool fully_active_locked() noexcept;

void render_frame_locked() noexcept;

void render_present_frame_locked() noexcept;

} // namespace dawn::client::hooks::graphics::renderer
