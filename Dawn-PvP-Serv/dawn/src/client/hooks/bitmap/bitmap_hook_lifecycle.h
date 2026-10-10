#pragma once

namespace dawn::client::hooks::bitmap {

[[nodiscard]] bool install() noexcept;

void uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

} // namespace dawn::client::hooks::bitmap
