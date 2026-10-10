#pragma once

#include <cstdint>

namespace dawn::server {

[[nodiscard]] bool initialize_dedicated() noexcept;

void service(std::uint64_t now) noexcept;

void shutdown() noexcept;

} // namespace dawn::server
