#pragma once

#include <span>

#include "../../../../state/build_data/progressions/definition.h"
#include "layout.h"

namespace dawn::middleware::datagen::family4::progression {

// slots start at the empty index: 0 is a real progression and the enumerator skips a slot
// only when all bits are set. keyed slots are this object's scope, in native order
[[nodiscard]] bool key_bank(state::build_data::progressions::Scope scope,
                            std::span<layout::Entry> bank) noexcept;

} // namespace dawn::middleware::datagen::family4::progression
