#pragma once

#include "../../platform/sdk.h"

namespace dawn::client::hooks::egress::winsock::transmission {

int WSAAPI send_bytes(SOCKET socket, const char* buffer, int length, int flags) noexcept;

int WSAAPI send_buffers(SOCKET socket,
                        LPWSABUF buffers,
                        DWORD bufferCount,
                        LPDWORD bytesSent,
                        DWORD flags,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept;

int WSAAPI send_bytes_to(SOCKET socket,
                         const char* buffer,
                         int length,
                         int flags,
                         const sockaddr* destination,
                         int destinationLength) noexcept;

int WSAAPI send_buffers_to(SOCKET socket,
                           LPWSABUF buffers,
                           DWORD bufferCount,
                           LPDWORD bytesSent,
                           DWORD flags,
                           const sockaddr* destination,
                           int destinationLength,
                           LPWSAOVERLAPPED overlapped,
                           LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept;

INT PASCAL send_message(SOCKET socket,
                        LPWSAMSG message,
                        DWORD flags,
                        LPDWORD bytesSent,
                        LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) noexcept;

} // namespace dawn::client::hooks::egress::winsock::transmission
