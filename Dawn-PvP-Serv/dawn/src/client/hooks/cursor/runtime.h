#pragma once

namespace dawn::client::hooks::cursor {

// the game recentres and confines the pointer every frame, which makes it unusable over an
// open interface, so both calls are answered while it is open
[[nodiscard]] bool install() noexcept;

void uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

void apply_visibility(bool visible) noexcept;

void service_native_cursor_tail() noexcept;
void request_native_clip_guard() noexcept;
[[nodiscard]] bool native_clip_guard_attached() noexcept;
[[nodiscard]] bool prepare_native_tail_shutdown() noexcept;
[[nodiscard]] bool prepare_native_tail_retirement() noexcept;

} // namespace dawn::client::hooks::cursor
