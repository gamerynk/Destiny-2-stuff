#pragma once

namespace dawn::client::hooks::egress {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool is_installed() noexcept;

void report_installation() noexcept;

} // namespace dawn::client::hooks::egress
