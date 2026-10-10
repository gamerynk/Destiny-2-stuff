#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "../../middleware/web_service/messages/opcode206.h"
#include "../../state/runtime/runtime.h"
#include "../../state/vendors/answered_interactions.h"

namespace dawn::server::web_service {

struct Outcome {
    bool hasSubscription{};
    middleware::queuez::Subscription subscription{};
    bool hasSelectedCharacter{};
    std::uint64_t selectedCharacterSoid{};
    using Mutation = std::variant<std::monostate,
                                  state::PendingEquipmentSwap,
                                  state::PendingItemAcquisition,
                                  state::PendingProfileItemAcquisition,
                                  state::PendingItemDismantle,
                                  state::PendingSocketPlug,
                                  state::PendingItemState,
                                  state::PendingVaultTransfer,
                                  state::PendingAbilitySelection>;
    Mutation mutation{};
    // vendor interaction answered once this request's mutation commits; the answered list is append-only
    // for the session, so answering before commit could bury a quest the player is still owed
    std::uint16_t answeredVendor{state::vendors::kAbsentIndex};
};

inline void reset(Outcome& outcome) noexcept {
    outcome.hasSubscription = false;
    outcome.subscription = {};
    outcome.hasSelectedCharacter = false;
    outcome.selectedCharacterSoid = 0;
    outcome.answeredVendor = state::vendors::kAbsentIndex;
    outcome.mutation.emplace<std::monostate>();
}

template <typename Mutation>
[[nodiscard]] const Mutation* mutation_if(const Outcome& outcome) noexcept {
    return std::get_if<Mutation>(&outcome.mutation);
}

void report_equip_response(const middleware::web_service::Message& message,
                           std::int32_t family4Version,
                           std::span<const std::byte> response) noexcept;

void report_item_acquisition_response(const middleware::web_service::Message& message,
                                      std::int32_t family4Version,
                                      std::uint64_t acquiredInstanceSoid,
                                      std::span<const std::byte> response) noexcept;

void report_profile_item_acquisition_response(const middleware::web_service::Message& message,
                                              std::int32_t family4Version,
                                              std::uint32_t definitionHash,
                                              std::int32_t quantity,
                                              std::span<const std::byte> response) noexcept;

void report_item_dismantle_response(const middleware::web_service::Message& message,
                                    std::int32_t family4Version,
                                    std::uint64_t dismantledInstanceSoid,
                                    std::span<const std::byte> response) noexcept;

void report_socket_plug_response(const middleware::web_service::Message& message,
                                 std::int32_t family4Version,
                                 std::uint64_t targetInstanceSoid,
                                 std::uint8_t socketLane,
                                 std::uint16_t plugDefinitionIndex,
                                 std::span<const std::byte> response) noexcept;

[[nodiscard]] bool consume(std::span<const std::byte> request,
                           std::span<std::byte> response,
                           std::size_t& written) noexcept;

[[nodiscard]] bool consume(std::span<const std::byte> request,
                           std::span<std::byte> response,
                           std::size_t& written,
                           Outcome& outcome) noexcept;

} // namespace dawn::server::web_service
