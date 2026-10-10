#pragma once

namespace dawn::client::hooks::queuez {

[[nodiscard]] bool install_null_payload_guard() noexcept;

void uninstall_null_payload_guard() noexcept;

[[nodiscard]] bool null_payload_guard_installed() noexcept;

} // namespace dawn::client::hooks::queuez
