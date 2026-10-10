#pragma once

#include <cstdint>

namespace dawn::client::hooks::queuez {

[[nodiscard]] bool install() noexcept;

void uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

[[nodiscard]] bool native_subscription_requested() noexcept;

[[nodiscard]] std::uint64_t processed_update_generation() noexcept;

} // namespace dawn::client::hooks::queuez
