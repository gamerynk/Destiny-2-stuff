#pragma once

namespace dawn::client::hooks::package_trust {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

[[nodiscard]] bool has_ownership() noexcept;

} // namespace dawn::client::hooks::package_trust
