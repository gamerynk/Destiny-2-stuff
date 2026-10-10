#pragma once

#include <cstddef>
#include <span>

namespace dawn::server::http::owner_dashboard {

[[nodiscard]] bool render(std::span<std::byte> output, std::size_t& written) noexcept;

} // namespace dawn::server::http::owner_dashboard
