#pragma once

#include <span>

#include "../network.h"

namespace dawn::client::targets::game::network::content_derivation {

[[nodiscard]] bool derive(std::span<const patterns::ImageRange> image,
                          std::byte* gate,
                          Targets& resolved) noexcept;

} // namespace dawn::client::targets::game::network::content_derivation
