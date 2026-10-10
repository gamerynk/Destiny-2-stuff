#pragma once

#include <span>

#include "../../../patterns/registry.h"
#include "../config_getter.h"

namespace dawn::client::targets::game::config_getter {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image, Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::config_getter
