#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/gameplay/definition.h"

namespace dawn::server::gameplay::dtls {

[[nodiscard]] bool route(const state::gameplay::Endpoint& from,
                         std::span<const std::byte> datagram,
                         std::uint64_t now) noexcept;

[[nodiscard]] bool send_payload(const state::gameplay::Endpoint& to,
                                std::span<const std::byte> payload) noexcept;

void register_security_key(const state::gameplay::Endpoint& endpoint,
                           std::uint64_t securityId,
                           std::span<const std::byte> securityKey) noexcept;

void prefer_security_id(const state::gameplay::Endpoint& endpoint,
                        std::uint64_t securityId) noexcept;

void reset() noexcept;

} // namespace dawn::server::gameplay::dtls
