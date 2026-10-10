#pragma once

#include <span>

#include "../../../patterns/registry.h"
#include "../packages.h"

namespace dawn::client::targets::game::packages {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image, Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::packages
