#pragma once

#include <cstdint>

namespace dawn::steam::runtime::social {

[[nodiscard]] bool refresh() noexcept;

void activate() noexcept;

void service(std::uint64_t nowMilliseconds) noexcept;

void shutdown() noexcept;

} // namespace dawn::steam::runtime::social
