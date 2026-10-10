#pragma once

#include <cstdint>

#include "../internal.h"

namespace dawn::middleware::signon::extended {

[[nodiscard]] bool append(Writer& success, std::uint32_t relayAddress) noexcept;

} // namespace dawn::middleware::signon::extended
