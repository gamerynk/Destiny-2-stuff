#pragma once

namespace dawn::client::hooks::network::investment {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

[[nodiscard]] bool has_ownership() noexcept;

} // namespace dawn::client::hooks::network::investment
