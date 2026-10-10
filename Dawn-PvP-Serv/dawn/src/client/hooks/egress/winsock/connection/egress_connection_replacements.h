#pragma once

#include "../../platform/sdk.h"

namespace dawn::client::hooks::egress::winsock::connection {

int WSAAPI connect_socket(SOCKET socket, const sockaddr* name, int nameLength) noexcept;

} // namespace dawn::client::hooks::egress::winsock::connection
