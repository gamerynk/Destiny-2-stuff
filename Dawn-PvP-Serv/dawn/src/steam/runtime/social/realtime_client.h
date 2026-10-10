#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/social/realtime_codec.h"

namespace dawn::steam::runtime::social::realtime_client {

namespace wire = middleware::social::realtime;

struct PendingInvite final {
    std::uint64_t senderPlatformId{};
    std::uint64_t lobbyId{};
    std::array<char, 256> connect{};
    bool active{};
};

struct CentralGameplayBootstrapSnapshot final {
    std::uint64_t lobbyId{};
    std::uint64_t sequence{};
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize> descriptor{};
    bool active{};
};

struct SilentOwnerGameplayAdmissionSnapshot final {
    std::uint64_t lobbyId{};
    std::uint64_t sequence{};
    std::uint64_t sourcePlatformId{};
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize> descriptor{};
    bool active{};
};

[[nodiscard]] bool execute(wire::Request request, wire::Response& response) noexcept;

[[nodiscard]] bool stage_rich_presence(const char* key, const char* value) noexcept;
void stage_clear_rich_presence() noexcept;

[[nodiscard]] wire::Lobby lobby_snapshot() noexcept;

[[nodiscard]] wire::Lobby lobby_snapshot(std::uint64_t lobbyId,
                                        bool refreshIfNeeded = false) noexcept;

[[nodiscard]] bool stage_native_fireteam_descriptor(
    std::uint64_t lobbyId,
    std::span<const std::byte> descriptor) noexcept;

[[nodiscard]] CentralGameplayBootstrapSnapshot central_gameplay_bootstrap_snapshot() noexcept;

[[nodiscard]] bool begin_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                                          std::uint64_t sequence) noexcept;

[[nodiscard]] bool complete_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                                             std::uint64_t sequence) noexcept;

void cancel_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                             std::uint64_t sequence) noexcept;

[[nodiscard]] SilentOwnerGameplayAdmissionSnapshot
silent_owner_gameplay_admission_snapshot() noexcept;

[[nodiscard]] bool begin_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                                         std::uint64_t sequence) noexcept;

[[nodiscard]] bool complete_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                                            std::uint64_t sequence) noexcept;

void cancel_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                            std::uint64_t sequence) noexcept;

[[nodiscard]] bool consume_internal_owner_leave_suppression(std::uint64_t lobbyId) noexcept;

[[nodiscard]] bool stage_membership_projection(
    std::span<const std::byte> decodedMembership) noexcept;

[[nodiscard]] PendingInvite pending_invite_snapshot() noexcept;

[[nodiscard]] bool accept_invite() noexcept;

void decline_invite() noexcept;

void report_owner_gameplay_admission_dispatch(const void* payload,
                                              std::size_t payloadSize,
                                              std::size_t registrations) noexcept;

void report_owner_gameplay_admission_dispatch_return(const void* payload,
                                                     std::size_t payloadSize,
                                                     std::size_t registrations) noexcept;

void report_owner_gameplay_admission_launch(std::uint64_t targetPlatformId,
                                            bool accepted) noexcept;

[[nodiscard]] int copy_chat_entry(std::uint64_t lobbyId,
                                  int messageId,
                                  std::uint64_t* sender,
                                  void* data,
                                  int dataCapacity,
                                  int* entryType) noexcept;

void activate() noexcept;

void service(std::uint64_t nowMilliseconds) noexcept;

void shutdown() noexcept;

} // namespace dawn::steam::runtime::social::realtime_client
