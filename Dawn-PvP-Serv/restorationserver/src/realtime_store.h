#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "middleware/gameplay/descriptor/join_descriptor.h"
#include "middleware/social/realtime_codec.h"
#include "state/social/definition.h"

namespace restoration::realtime_store {

struct Presence final {
    std::uint64_t accountSoid{};
    std::uint64_t lobbyId{};
    bool online{};
    bool joinable{};
    std::array<char, dawn::state::social::kLocationCapacity> status{};
    std::array<char, dawn::state::social::kSteamDisplayCapacity> steamDisplay{};
    std::array<char, dawn::state::social::kConnectCapacity> connect{};
    std::array<char, dawn::state::social::kPlayerGroupCapacity> playerGroup{};
    std::array<char, dawn::state::social::kPlayerGroupSizeCapacity> playerGroupSize{};
};

struct StatusPlayer final {
    std::uint64_t platformId{};
    std::uint64_t accountSoid{};
    std::uint64_t lobbyId{};
    std::array<char, dawn::state::social::kLocationCapacity> status{};
    std::array<char, dawn::state::social::kSteamDisplayCapacity> steamDisplay{};
    std::array<std::uint64_t, dawn::middleware::social::realtime::kMemberCapacity>
        teamPlatformIds{};
    std::size_t teamCount{};
};

struct StatusSnapshot final {
    std::array<StatusPlayer, 128> players{};
    std::size_t playerCount{};
};

using FriendshipCheck = bool (*)(std::uint64_t leftPlatformId,
                                 std::uint64_t rightPlatformId) noexcept;

enum class DiagnosticLevel : std::uint8_t {
    info,
    warning,
};

using DiagnosticSink = void (*)(DiagnosticLevel level, std::string_view event) noexcept;

using GameplayBootstrapDescriptor = std::array<
    std::byte,
    dawn::middleware::gameplay::descriptor::kDescriptorSize>;

using GameplayBootstrapDescriptorProvider = bool (*)(
    std::uint64_t lobbyId,
    GameplayBootstrapDescriptor& output) noexcept;

void set_gameplay_bootstrap_descriptor_provider(
    GameplayBootstrapDescriptorProvider provider) noexcept;

[[nodiscard]] bool native_fireteam_descriptor(
    std::uint64_t lobbyId,
    std::uint64_t ownerPlatformId,
    GameplayBootstrapDescriptor& output) noexcept;

[[nodiscard]] bool begin_game_session(std::uint64_t platformId,
                                      std::uint64_t accountSoid,
                                      std::uint64_t sessionGeneration,
                                      std::uint64_t nowSeconds) noexcept;

[[nodiscard]] bool mark_gameplay_ready(std::uint64_t platformId,
                                       std::uint64_t accountSoid,
                                       std::uint64_t sessionGeneration,
                                       std::uint64_t nowSeconds) noexcept;

[[nodiscard]] bool presence(std::uint64_t platformId,
                            std::uint64_t nowSeconds,
                            Presence& output) noexcept;

[[nodiscard]] bool status_snapshot(std::uint64_t nowSeconds,
                                   StatusSnapshot& output) noexcept;

[[nodiscard]] bool lobby_for_member(
    std::uint64_t platformId,
    dawn::middleware::social::realtime::Lobby& output) noexcept;

[[nodiscard]] bool lobby_for_account(
    std::uint64_t accountSoid,
    dawn::middleware::social::realtime::Lobby& output) noexcept;

// takes the realtime lock: callers must not hold the account-store lock
[[nodiscard]] bool shares_fireteam(std::uint64_t leftPlatformId,
                                  std::uint64_t rightPlatformId) noexcept;
[[nodiscard]] bool shares_fireteam_accounts(std::uint64_t leftAccountSoid,
                                           std::uint64_t rightAccountSoid) noexcept;

[[nodiscard]] bool lobby_snapshot(
    std::uint64_t lobbyId,
    dawn::middleware::social::realtime::Lobby& output) noexcept;

[[nodiscard]] bool queue_owner_gameplay_admission(std::uint64_t lobbyId,
                                                   std::uint64_t sourcePlatformId) noexcept;

[[nodiscard]] bool remove_lobby_member(std::uint64_t lobbyId,
                                       std::uint64_t actorPlatformId,
                                       std::uint64_t targetPlatformId,
                                       dawn::middleware::social::realtime::Lobby* updatedLobby =
                                           nullptr) noexcept;

[[nodiscard]] bool execute(
    std::uint64_t platformId,
    std::uint64_t accountSoid,
    std::uint64_t nowSeconds,
    const dawn::middleware::social::realtime::Request& request,
    FriendshipCheck friendshipCheck,
    dawn::middleware::social::realtime::Response& response,
    DiagnosticSink diagnosticSink = nullptr) noexcept;

void shutdown() noexcept;

} // namespace restoration::realtime_store
