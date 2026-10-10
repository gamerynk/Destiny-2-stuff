#pragma once

#include "../platform/sdk.h"

namespace dawn::client::hooks::egress::resolver {

INT WSAAPI address_info_a(PCSTR node, PCSTR service, const ADDRINFOA* hints,
                         PADDRINFOA* result) noexcept;

INT WSAAPI address_info_w(PCWSTR node,
                          PCWSTR service,
                          const ADDRINFOW* hints,
                          PADDRINFOW* result) noexcept;

INT WSAAPI address_info_ex_w(PCWSTR name,
                             PCWSTR service,
                             DWORD nameSpace,
                             LPGUID provider,
                             const ADDRINFOEXW* hints,
                             PADDRINFOEXW* result,
                             timeval* timeout,
                             LPOVERLAPPED overlapped,
                             LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
                             LPHANDLE nameHandle) noexcept;

hostent* WSAAPI host_by_name(const char* name) noexcept;

hostent* WSAAPI host_by_address(const char* address, int length, int type) noexcept;

INT WSAAPI name_info_a(const SOCKADDR* address,
                       socklen_t addressLength,
                       PCHAR node,
                       DWORD nodeSize,
                       PCHAR service,
                       DWORD serviceSize,
                       INT flags) noexcept;

INT WSAAPI name_info_w(const SOCKADDR* address,
                       socklen_t addressLength,
                       PWCHAR node,
                       DWORD nodeSize,
                       PWCHAR service,
                       DWORD serviceSize,
                       INT flags) noexcept;

} // namespace dawn::client::hooks::egress::resolver
