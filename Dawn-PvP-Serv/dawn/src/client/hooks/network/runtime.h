#pragma once

namespace dawn::client::hooks::network {

[[nodiscard]] bool install_game() noexcept;

[[nodiscard]] bool install_platform() noexcept;

[[nodiscard]] bool service_platform_policy() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_game_installed() noexcept;

[[nodiscard]] bool has_game_ownership() noexcept;

[[nodiscard]] bool is_platform_installed() noexcept;

[[nodiscard]] bool is_installed() noexcept;

} // namespace dawn::client::hooks::network
