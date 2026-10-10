#pragma once

namespace dawn::client::hooks::network::content_config {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

[[nodiscard]] bool has_ownership() noexcept;

[[nodiscard]] bool prepare_native_request() noexcept;

} // namespace dawn::client::hooks::network::content_config
