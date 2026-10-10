#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../state/gameplay/definition.h"

namespace dawn::server::gameplay::endpoint {

inline constexpr std::uint64_t kLobbyMachineDomain = 0xD1B54A32D192ED03ULL;

// an odd multiplier is a permutation mod 2^64, so distinct nonzero lobby ids stay distinct and none maps to zero
[[nodiscard]] constexpr std::uint64_t derive_lobby_machine_id(
    std::uint64_t endpointMachineId,
    std::uint64_t lobbyId) noexcept {
    if (endpointMachineId == 0 || lobbyId == 0) {
        return 0;
    }
    const std::uint64_t odd = (endpointMachineId ^ kLobbyMachineDomain) | 1ULL;
    return lobbyId * odd;
}

// the odd multiplier is invertible mod 2^64; six Newton steps take 1 correct bit to all 64
// keeps first-endpoint routing exact when two accounts bootstrap solo lobbies on one endpoint
[[nodiscard]] constexpr std::uint64_t recover_lobby_id(
    std::uint64_t endpointMachineId,
    std::uint64_t gameplayMachineId) noexcept {
    if (endpointMachineId == 0 || gameplayMachineId == 0) {
        return 0;
    }
    const std::uint64_t odd = (endpointMachineId ^ kLobbyMachineDomain) | 1ULL;
    std::uint64_t inverse = 1;
    for (unsigned iteration = 0; iteration < 6; ++iteration) {
        inverse *= std::uint64_t{2} - odd * inverse;
    }
    return gameplayMachineId * inverse;
}

[[nodiscard]] bool initialize() noexcept;

void service(std::uint64_t now) noexcept;

[[nodiscard]] bool send_to(const state::gameplay::Endpoint& destination,
                           std::span<const std::byte> datagram) noexcept;

void shutdown() noexcept;

[[nodiscard]] bool ready() noexcept;

[[nodiscard]] state::gameplay::Endpoint advertised() noexcept;

struct Identity {
    std::uint64_t machineId{};
    std::uint64_t onlineSessionId{};
};

[[nodiscard]] Identity identity() noexcept;

[[nodiscard]] std::uint64_t lobby_machine_id(std::uint64_t lobbyId) noexcept;

// Family 7 and the post-lobby bootstrap event must use this same value or they pick different transports
[[nodiscard]] bool build_lobby_descriptor(
    std::uint64_t lobbyId,
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize>& output) noexcept;

} // namespace dawn::server::gameplay::endpoint
