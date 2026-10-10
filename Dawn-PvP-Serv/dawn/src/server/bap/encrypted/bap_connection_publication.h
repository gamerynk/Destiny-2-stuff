#pragma once

#include <cstdint>

#include "../internal.h"
#include "internal.h"
#include "queuez/queuez_state_validation.h"
#include "transactions/definition.h"

namespace dawn::server::bap::encrypted {

struct ConnectionFields {
    middleware::bap::activity_message::patch_epoch::PatchEpoch patchEpoch{};
    state::activity::entity_slots::LeaseMask joinEntitySlots{};
    std::uint64_t joinMemberKey{};
    std::uint64_t joinCharacterSoid{};
    std::uint64_t joinSessionId{};
    std::uint64_t retirementJoinRevision{};
    std::uint64_t publishedRetirementSerial{};
    std::uint32_t activityMessageType{};
    std::uint32_t joinCorrelation{};
    std::int32_t authoritativeRegion{-1};
    std::uint8_t authoritativeTransitionToken{};
    state::activity::membership::MutationKind membershipKind{};
    bool retainsPatchEpoch{};
    bool observesActivityMessage{};
    bool authoritativeHasRegion{};
    bool authoritativeHasTransitionToken{};
    bool opensTransitionWindow{};
    bool joinsActivity{};
};

[[nodiscard]] ConnectionFields connection_fields(const ServiceOutcome& outcome) noexcept;

void publish_connection_fields(Session& session,
                               const transactions::Publication& publication,
                               const ConnectionFields& fields) noexcept;

void arm_repushes(Session& session, const queuez::StagedPublication& queuezPublication) noexcept;

} // namespace dawn::server::bap::encrypted
