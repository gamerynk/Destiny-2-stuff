#pragma once

#include <cstdint>

namespace dawn::server::bap::encrypted::push::activity {

inline constexpr std::uint64_t kPublicMembershipSettleDelayMs = 300;

[[nodiscard]] constexpr std::uint64_t
public_membership_settle_deadline(std::uint64_t readyAtTick) noexcept {
    return readyAtTick + kPublicMembershipSettleDelayMs;
}

[[nodiscard]] constexpr bool public_membership_settled(
    std::uint64_t now,
    std::uint64_t notBeforeTick) noexcept {
    return notBeforeTick != 0 && now >= notBeforeTick;
}

enum class MembershipPublicationPhase : std::uint8_t {
    none,
    baseline,
    advertisement,
    steady,
};

[[nodiscard]] constexpr bool primary_region_publication_complete(
    bool membershipSent, bool includesAdvertisement, bool advertisementReady,
    bool socialPrimary, bool advertisementAbsent) noexcept {
    return membershipSent && includesAdvertisement
           && (advertisementReady || (socialPrimary && advertisementAbsent));
}

// retail accepts one membership body per revision; a descriptor-free body during baseline spends it and retail drops the following advertisement as a duplicate
[[nodiscard]] constexpr MembershipPublicationPhase membership_publication_phase(
    bool hasSnapshot,
    std::int32_t region,
    std::int32_t baselineRegion,
    std::int32_t advertisedRegion) noexcept {
    if (!hasSnapshot) {
        return MembershipPublicationPhase::none;
    }
    if (region < 0) {
        return MembershipPublicationPhase::steady;
    }
    if (baselineRegion != region) {
        return MembershipPublicationPhase::baseline;
    }
    if (advertisedRegion != region) {
        return MembershipPublicationPhase::advertisement;
    }
    return MembershipPublicationPhase::steady;
}

[[nodiscard]] constexpr bool membership_phase_sends_snapshot(
    MembershipPublicationPhase phase) noexcept {
    return phase == MembershipPublicationPhase::advertisement
           || phase == MembershipPublicationPhase::steady;
}

[[nodiscard]] constexpr bool public_membership_should_publish(
    bool setupSettled, bool hasSnapshot, MembershipPublicationPhase phase,
    bool firstMembershipPublished, bool currentRevisionAcknowledged) noexcept {
    return setupSettled && hasSnapshot && membership_phase_sends_snapshot(phase)
           && (phase == MembershipPublicationPhase::advertisement
               || !firstMembershipPublished || !currentRevisionAcknowledged);
}

} // namespace dawn::server::bap::encrypted::push::activity
