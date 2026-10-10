#pragma once

#include <cstdint>
#include <array>
#include <span>

#include "../../client/network/consumer.h"

namespace dawn::server::bap {

struct ConnectionStatus final {
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t activitySessionId{};
    std::uint64_t openedAtTick{};
    std::uint64_t lastFrameAtTick{};
    std::uint32_t connectionId{};
    bool authenticated{};
    bool activityJoined{};
};

[[nodiscard]] bool consume(const client::network::BapRequest& request,
                           client::network::BapResponse& response) noexcept;

[[nodiscard]] std::size_t
snapshot_connections(std::span<ConnectionStatus> output) noexcept;

struct NativeActivityView final {
    std::uint64_t account{}, activity{}, member{}, connectionIncarnation{};
    std::uint64_t createdRevision{}, joinedRevision{}, retirementJoinRevision{};
    std::array<std::uint64_t, 2> commonIdentity{};
    std::uint32_t connectionId{};
    // published serial, not a consumption ack; the native wire uses its low byte
    std::uint64_t publishedRetirementSerial{};
    std::uint32_t scenarioTag{}, nativeMapHash{};
    std::array<std::byte,1024> serverEntitySlots{};
    bool publicWorld{};
};
[[nodiscard]] bool snapshot_native_activity_views(std::uint64_t account,
    std::span<NativeActivityView> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot_native_membership_views(std::uint64_t account,
    std::span<NativeActivityView> output,std::size_t& count) noexcept;

void social_projection_changed() noexcept;

void account_roster_changed(std::uint64_t accountSoid) noexcept;
void account_inventory_acquired(std::uint64_t accountSoid, std::uint64_t characterSoid,
                                std::uint64_t instanceSoid) noexcept;

[[nodiscard]] bool with_account_transaction(bool (*operation)(void*) noexcept,
                                             void* context) noexcept;

[[nodiscard]] bool roster_presence_self_test(std::uint64_t ownerActivitySessionId) noexcept;
[[nodiscard]] bool inventory_feedback_publication_self_test() noexcept;

void shutdown() noexcept;

} // namespace dawn::server::bap
