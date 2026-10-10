#pragma once

#include <cstdint>

namespace dawn::client::content::investment::worker {

void activate() noexcept;

void service(std::uint64_t nowMilliseconds) noexcept;

void reset() noexcept;

} // namespace dawn::client::content::investment::worker
