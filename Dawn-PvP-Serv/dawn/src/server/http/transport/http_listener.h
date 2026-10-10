#pragma once

#include <cstdint>

namespace dawn::server::http::transport {

[[nodiscard]] bool initialize() noexcept;

void service() noexcept;

void shutdown() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace dawn::server::http::transport
