#pragma once

#include "../../core/logging/log.h"

namespace dawn::server::gameplay {

void report(core::log::Level level, const char* format, ...) noexcept;

} // namespace dawn::server::gameplay
