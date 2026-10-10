#pragma once

namespace dawn::client::hooks::bootflow {

[[nodiscard]] bool install() noexcept;

[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

void poll_world_step() noexcept;

[[nodiscard]] bool in_world() noexcept;

} // namespace dawn::client::hooks::bootflow
