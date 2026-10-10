#pragma once
#include <cstdint>
#include "../core/settings/settings.h"

namespace dawn::server {
// the trusted bot/owner listener must not use this helper
[[nodiscard]] inline std::uint32_t public_bind_host_order() noexcept {
    const auto& address = core::settings::get().server.gameplay.bindAddress;
    return (std::uint32_t(address[0]) << 24) | (std::uint32_t(address[1]) << 16)
         | (std::uint32_t(address[2]) << 8) | std::uint32_t(address[3]);
}
}
