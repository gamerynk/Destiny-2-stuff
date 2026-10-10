#pragma once

#include <Windows.h>

#include <dxgi.h>
#include <d3d11.h>

namespace dawn::client::hooks::graphics::renderer {

void present(IDXGISwapChain* swapChain, ID3D11DeviceContext* nativeContext = nullptr) noexcept;

[[nodiscard]] bool prepare_native_frame(IDXGISwapChain*, ID3D11DeviceContext*) noexcept;
[[nodiscard]] bool submit_native_frame(IDXGISwapChain*, ID3D11DeviceContext*) noexcept;
void finish_native_frame() noexcept;
// same-thread reentry during preparation already owns the renderer lock
[[nodiscard]] bool handle_preparation_message(HWND, UINT, WPARAM, LPARAM) noexcept;

void before_surface_change(IDXGISwapChain* swapChain) noexcept;

void after_surface_change(IDXGISwapChain* swapChain, HRESULT result) noexcept;

void present_result(IDXGISwapChain* swapChain, HRESULT result) noexcept;

void check_native_device_loss() noexcept;

[[nodiscard]] bool service_pending_shutdown() noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] bool active() noexcept;

[[nodiscard]] bool
handle_window_message(HWND window, UINT message, WPARAM word, LPARAM value) noexcept;

void dispatch_pending_input_release(HWND window) noexcept;

void dispatch_pending_input_release() noexcept;

} // namespace dawn::client::hooks::graphics::renderer
