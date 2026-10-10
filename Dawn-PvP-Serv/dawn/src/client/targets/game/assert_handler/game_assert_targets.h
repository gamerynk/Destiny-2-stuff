#pragma once

#include <span>

#include "../../../patterns/registry.h"
#include "../assert_handler.h"

namespace dawn::client::targets::game::assert_handler {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image, Targets& output) noexcept;

void publish(const Targets& targets) noexcept;

} // namespace dawn::client::targets::game::assert_handler
