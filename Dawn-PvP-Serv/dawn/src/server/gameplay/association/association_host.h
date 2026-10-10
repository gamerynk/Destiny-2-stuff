#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/gameplay/definition.h"

namespace dawn::server::gameplay::association {

void route(const state::gameplay::Endpoint& from,
           std::span<const std::byte> datagram,
           std::uint64_t now) noexcept;

[[nodiscard]] bool send_payload(const state::gameplay::Endpoint& to,
                                std::span<const std::byte> payload) noexcept;

void expire(std::uint64_t now) noexcept;

void reset() noexcept;

} // namespace dawn::server::gameplay::association
