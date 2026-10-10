#pragma once

#include <cstdint>
#include <Windows.h>

namespace dawn::client::hooks::polled_input {

// the game scans GetKeyState every frame for action keys instead of using window messages,
// so capturing messages alone doesn't stop it
[[nodiscard]] bool install() noexcept;

void uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

void apply_visibility(bool visible) noexcept;

void observe_native_focus() noexcept;

[[nodiscard]] bool native_pause_owned() noexcept;

[[nodiscard]] bool native_pause_available() noexcept;

[[nodiscard]] bool drain_native_key_state() noexcept;

[[nodiscard]] bool native_key_focus_available() noexcept;

void service_native_key_recovery() noexcept;

void request_native_key_guard() noexcept;
[[nodiscard]] bool native_key_guard_attached() noexcept;
[[nodiscard]] bool native_key_recovery_safe() noexcept;

LRESULT forward_with_native_focus(WNDPROC original, HWND window, UINT message,
                                 WPARAM word, LPARAM value) noexcept;
} // namespace dawn::client::hooks::polled_input
