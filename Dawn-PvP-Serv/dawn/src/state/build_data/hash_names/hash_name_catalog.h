#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::hash_names {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Name> names) noexcept;

[[nodiscard]] bool replace(std::span<const Name> names) noexcept;

[[nodiscard]] bool find(std::uint32_t hash, Name& name) noexcept;

[[nodiscard]] bool snapshot(std::span<Name> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::hash_names
