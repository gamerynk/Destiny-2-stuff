#pragma once

#include <cstdint>

namespace dawn::steam::runtime::social::initial_lobby_bootstrap {

inline constexpr std::int32_t kLobbyType = 1;

enum class Action : std::uint8_t {
    none,
    ensureSoloLobby,
};

struct Input final {
    bool dedicatedBackendRequired{};
    bool pollSucceeded{};
    std::uint64_t localPlatformId{};
    std::uint64_t currentLobbyId{};
};

// the first successful authenticated poll is the earliest safe point to create the solo
// lobby; it supplies kind 8 before boot task 8. The server op is conditional
[[nodiscard]] constexpr Action decide(Input input) noexcept {
    return input.dedicatedBackendRequired && input.pollSucceeded
                   && input.localPlatformId != 0 && input.currentLobbyId == 0
               ? Action::ensureSoloLobby
               : Action::none;
}

} // namespace dawn::steam::runtime::social::initial_lobby_bootstrap
