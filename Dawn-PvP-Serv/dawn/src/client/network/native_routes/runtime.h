#pragma once

#include <Windows.h>
#include "../../../core/settings/client/external/definition.h"

namespace dawn::client::network::native_routes {

// startup only; a validation failure leaves native destinations intact
[[nodiscard]] bool publish_discovery(HMODULE game,
    const core::settings::client::external::Settings& external) noexcept;

} // namespace dawn::client::network::native_routes
