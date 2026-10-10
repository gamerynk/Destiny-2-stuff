#pragma once

#include "../hooks/teleport/runtime.h"

namespace dawn::client::player::position {

struct Snapshot {
    hooks::teleport::Vector position{};
    bool present{};
};

void observe(void* component) noexcept;

void poll() noexcept;

void reset() noexcept;

[[nodiscard]] Snapshot snapshot() noexcept;

} // namespace dawn::client::player::position
