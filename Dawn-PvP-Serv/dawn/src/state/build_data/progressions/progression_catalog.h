#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::progressions {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool slots(Scope scope, std::span<std::uint16_t> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::progressions
