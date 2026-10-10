#pragma once

#include <cstdint>

#include "definition.h"

namespace dawn::state::activity::destination {

// a set declared only by a package the destination never loads cannot attach and the player
// arrives with no spawn point; send the absent hash so the client picks its own
[[nodiscard]] std::uint32_t attachable_spawn_set_hash(const DestinationSelection& selection,
                                                      std::uint32_t fallback) noexcept;

} // namespace dawn::state::activity::destination
