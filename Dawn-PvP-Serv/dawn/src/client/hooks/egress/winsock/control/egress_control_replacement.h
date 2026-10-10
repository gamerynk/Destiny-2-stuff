#pragma once

#include "../../platform/sdk.h"

namespace dawn::client::hooks::egress::winsock::control {

int WSAAPI socket_control(SOCKET socket,
                          DWORD controlCode,
                          LPVOID input,
                          DWORD inputSize,
                          LPVOID output,
                          DWORD outputSize,
                          LPDWORD bytesReturned,
                          LPWSAOVERLAPPED overlapped,
                          LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept;

} // namespace dawn::client::hooks::egress::winsock::control
