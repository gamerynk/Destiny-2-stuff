#pragma once

#include <Windows.h>

#include <cstdint>

#include "../runtime.h"

namespace dawn::steam::runtime::transport {

[[nodiscard]] bool initialize(DWORD appId) noexcept;

[[nodiscard]] std::uint64_t platform_id() noexcept;

[[nodiscard]] void* http_interface() noexcept;

[[nodiscard]] const char* country() noexcept;

[[nodiscard]] std::uint32_t server_real_time() noexcept;

[[nodiscard]] ApiCall request_certificate() noexcept;

[[nodiscard]] int network_config(void* output,
                                 DWORD capacity,
                                 const char* launcherPartner) noexcept;

void cache_relay_ticket(const void* ticket, DWORD ticketSize) noexcept;

[[nodiscard]] DWORD relay_ticket_count() noexcept;

[[nodiscard]] int relay_ticket(DWORD index, void* output, DWORD capacity) noexcept;

void post_connection_state(const void* message, DWORD messageSize) noexcept;

void service() noexcept;

void shutdown() noexcept;

} // namespace dawn::steam::runtime::transport
