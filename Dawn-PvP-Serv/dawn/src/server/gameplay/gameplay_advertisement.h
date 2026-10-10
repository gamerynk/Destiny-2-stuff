#pragma once

#include <cstdint>

#include "../../middleware/bap/activity_message/replicate_membership.h"

namespace dawn::server::gameplay {

namespace endpoint { struct Identity; }

[[nodiscard]] bool region_descriptor_identity(std::uint64_t sourceSessionId,
                                              std::int32_t regionIndex,
                                              const endpoint::Identity& base,
                                              endpoint::Identity& output) noexcept;

enum class RegionSource : std::uint8_t {
    reported,
    arrival,
};

void build_advertisement(
    std::uint64_t sourceSessionId,
    std::int32_t regionIndex,
    RegionSource regionSource,
    std::uint8_t localMemberSlot,
    middleware::bap::activity_message::replicate_membership::CitizenAdvertisement& output) noexcept;

enum class AdvertisementState : std::uint8_t {
    ready,
    pending,
    absent,
};

// the client applies one membership update per revision; publishing during pending wastes it on a
// region record with no descriptor, so a publisher must hold
[[nodiscard]] AdvertisementState advertisement_state(std::uint64_t sourceSessionId,
                                                     std::int32_t regionIndex) noexcept;

} // namespace dawn::server::gameplay
