#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::abilities {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool find(std::uint16_t socketEntryListIndex,
                        const Selection& selection,
                        Definition& definition) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

} // namespace dawn::state::build_data::abilities
