#pragma once

#include <cstdint>

namespace dawn::server::transport {

[[nodiscard]] bool initialize() noexcept;

void service(std::uint64_t now) noexcept;

void shutdown() noexcept;

} // namespace dawn::server::transport
