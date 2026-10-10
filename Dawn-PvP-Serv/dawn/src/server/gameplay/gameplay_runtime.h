#pragma once

#include <cstdint>

namespace dawn::server::gameplay {

[[nodiscard]] bool initialize() noexcept;

void service(std::uint64_t now) noexcept;

void shutdown() noexcept;

} // namespace dawn::server::gameplay
