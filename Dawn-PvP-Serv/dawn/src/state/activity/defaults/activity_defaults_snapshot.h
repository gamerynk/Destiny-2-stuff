#pragma once

#include "definition.h"

namespace dawn::state::activity::defaults {

void snapshot(ActivityDefaults& output) noexcept;

void apply_arrival_override(const ActivityDefaults& defaults,
                            destination::DestinationSelection& selection) noexcept;

} // namespace dawn::state::activity::defaults
