#pragma once

#include <cstddef>
#include <span>

#include "../../platform/sdk.h"

namespace dawn::client::hooks::egress::winsock::discovery {

using SendTo = decltype(&::sendto);

struct Result final {
    bool handled{};
    int result{};
};

[[nodiscard]] Result handle(SOCKET socket,
                            std::span<const std::byte> payload,
                            int flags,
                            const sockaddr* destination,
                            int destinationLength,
                            SendTo sendTo) noexcept;

} // namespace dawn::client::hooks::egress::winsock::discovery
