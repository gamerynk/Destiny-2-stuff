#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::client::hooks::external_server {

[[nodiscard]] bool enabled() noexcept;

[[nodiscard]] std::size_t rewrite_host(std::string_view url, std::span<char> output) noexcept;

[[nodiscard]] bool install() noexcept;

// drains admitted calls; keeps the live trampoline when detach fails
[[nodiscard]] bool uninstall() noexcept;

[[nodiscard]] bool is_installed() noexcept;

[[nodiscard]] bool has_ownership() noexcept;

} // namespace dawn::client::hooks::external_server
