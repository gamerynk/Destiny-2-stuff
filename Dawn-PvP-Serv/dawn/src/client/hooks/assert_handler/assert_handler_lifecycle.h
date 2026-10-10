#pragma once

namespace dawn::client::hooks::assert_handler {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

} // namespace dawn::client::hooks::assert_handler
