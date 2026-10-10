#pragma once

namespace dawn::client::hooks::graphics {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

void service_native_window() noexcept;

} // namespace dawn::client::hooks::graphics
