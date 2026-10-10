#pragma once

#include <string_view>

#include "snapshot.h"

namespace dawn::core::log::snapshot::internal {

void reset() noexcept;

void record(Channel channel, Level level, std::string_view text) noexcept;

} // namespace dawn::core::log::snapshot::internal
