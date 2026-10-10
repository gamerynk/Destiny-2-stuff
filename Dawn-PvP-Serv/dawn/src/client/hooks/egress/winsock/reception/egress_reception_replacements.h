#pragma once

#include "../../platform/sdk.h"

namespace dawn::client::hooks::egress::winsock::reception {

int WSAAPI receive_bytes_from(SOCKET socket,
                              char* buffer,
                              int length,
                              int flags,
                              sockaddr* source,
                              int* sourceLength) noexcept;

int WSAAPI receive_buffers_from(SOCKET socket,
                                LPWSABUF buffers,
                                DWORD bufferCount,
                                LPDWORD bytesReceived,
                                LPDWORD flags,
                                sockaddr* source,
                                LPINT sourceLength,
                                LPWSAOVERLAPPED overlapped,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept;

} // namespace dawn::client::hooks::egress::winsock::reception
