#pragma once

#include <type_traits>

#include "../dns/egress_dns_replacements.h"
#include "../extensions/egress_extension_replacements.h"
#include "../resolver/replacements.h"
#include "../winsock/replacements.h"

namespace dawn::client::hooks::egress::platform {

static_assert(
    std::is_convertible_v<decltype(&winsock::connection::connect_socket), decltype(&::connect)>);
static_assert(
    std::is_convertible_v<decltype(&winsock::transmission::send_bytes), decltype(&::send)>);
static_assert(
    std::is_convertible_v<decltype(&winsock::transmission::send_buffers), decltype(&::WSASend)>);
static_assert(
    std::is_convertible_v<decltype(&winsock::transmission::send_bytes_to), decltype(&::sendto)>);
static_assert(std::is_convertible_v<decltype(&winsock::transmission::send_buffers_to),
                                    decltype(&::WSASendTo)>);
static_assert(
    std::is_convertible_v<decltype(&winsock::transmission::send_message), decltype(&::WSASendMsg)>);
static_assert(
    std::is_convertible_v<decltype(&extensions::transmit_file), decltype(&::TransmitFile)>);
static_assert(std::is_convertible_v<decltype(&winsock::control::socket_control), decltype(&::WSAIoctl)>);
static_assert(std::is_convertible_v<decltype(&winsock::reception::receive_bytes_from), decltype(&::recvfrom)>);
static_assert(std::is_convertible_v<decltype(&winsock::reception::receive_buffers_from), decltype(&::WSARecvFrom)>);
static_assert(std::is_convertible_v<decltype(&resolver::address_info_a), decltype(&::getaddrinfo)>);
static_assert(
    std::is_convertible_v<decltype(&resolver::address_info_w), decltype(&::GetAddrInfoW)>);
static_assert(
    std::is_convertible_v<decltype(&resolver::address_info_ex_w), decltype(&::GetAddrInfoExW)>);
static_assert(std::is_convertible_v<decltype(&resolver::host_by_name), decltype(&::gethostbyname)>);
static_assert(
    std::is_convertible_v<decltype(&resolver::host_by_address), decltype(&::gethostbyaddr)>);
static_assert(std::is_convertible_v<decltype(&resolver::name_info_a), decltype(&::getnameinfo)>);
static_assert(std::is_convertible_v<decltype(&resolver::name_info_w), decltype(&::GetNameInfoW)>);
static_assert(std::is_convertible_v<decltype(&dns::query_ex), decltype(&::DnsQueryEx)>);

} // namespace dawn::client::hooks::egress::platform
