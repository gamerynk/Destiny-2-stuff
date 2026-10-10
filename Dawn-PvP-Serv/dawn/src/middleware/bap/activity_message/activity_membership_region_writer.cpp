#include "replicate_membership.h"

namespace dawn::middleware::bap::activity_message::replicate_membership {
namespace {

constexpr std::size_t kRegionCount = 64;
constexpr std::uint32_t kRegionIndexStride = 8;
constexpr std::int32_t kMaximumRegionIndex =
    static_cast<std::int32_t>(kRegionCount * kRegionIndexStride - 1U);
constexpr std::uint32_t kRegionIndexBias = 0x80000000U;
constexpr std::size_t kRegionTokenCount = 32;
constexpr std::uint8_t kStateBitWidth = 3;
constexpr std::uint8_t kSliceSetBitWidth = 10;
constexpr std::int32_t kSignedFieldBias = 1;

constexpr std::uint64_t kAmbassadorAssigned = 2;
constexpr std::uint8_t kSlotBitWidth = 6;
// ambassador slot for a record with no advertisement, bias 1 (wire 2 = slot 1). Never
// the client's own slot 0: it would claim the region and a differing slot revokes it
constexpr std::uint64_t kUnadvertisedAmbassadorSlot = 2;
constexpr std::uint64_t kDescriptorCount = 128;

// the citizen's planned region wins once present; before that the reflected teleport slice
// keeps the selected state visible to the client
[[nodiscard]] std::int32_t
active_region_index(const MembershipSnapshot& snapshot) noexcept {
    const std::int32_t citizenRegion = snapshot.citizen.regionIndex;
    if (snapshot.citizen.present && citizenRegion >= 0
        && citizenRegion <= kMaximumRegionIndex) {
        return citizenRegion;
    }
    const std::int32_t teleportRegion = snapshot.teleport.sliceSetIndex;
    return teleportRegion >= 0 && teleportRegion <= kMaximumRegionIndex ? teleportRegion : -1;
}

[[nodiscard]] bool write_region(encoding::bits::Writer& writer,
                                std::size_t bubble,
                                const MembershipSnapshot& snapshot) noexcept {
    const std::uint32_t stateZeroRegion =
        static_cast<std::uint32_t>(bubble) * kRegionIndexStride;
    const std::int32_t activeRegion = active_region_index(snapshot);
    const bool activeBubble = activeRegion >= 0
                              && static_cast<std::uint32_t>(activeRegion)
                                         / kRegionIndexStride
                                     == bubble;
    const std::uint32_t region =
        activeBubble ? static_cast<std::uint32_t>(activeRegion) : stateZeroRegion;
    const bool advertise = snapshot.citizen.present
                           && static_cast<std::int32_t>(region) == snapshot.citizen.regionIndex;
    const std::uint64_t ambassadorSlot =
        advertise ? static_cast<std::uint64_t>(snapshot.citizen.ambassadorSlot) + kSignedFieldBias
                  : (snapshot.localMemberSlot==1 ? 1U : kUnadvertisedAmbassadorSlot);
    bool encoded = writer.write(kRegionIndexBias + region, 32) && writer.write(1, 2)
                   && writer.write(0, 8) && writer.write(kAmbassadorAssigned, 2)
                   && writer.write(ambassadorSlot, kSlotBitWidth) && writer.write(0, 1)
                   && writer.write(0, 32) && writer.write(0, 8) && writer.write(0, 32);
    for (std::size_t member = 0; encoded && member < kRegionTokenCount; ++member) {
        encoded = writer.write(snapshot.hasMemberTransitionTokens
            ? snapshot.memberTransitionTokens[member] : snapshot.transitionToken, 8);
    }
    if (!advertise) {
        return encoded && writer.write(0, 8) && writer.write(0, 64);
    }
    encoded = encoded && writer.write(kDescriptorCount, 8);
    for (const std::byte value : snapshot.citizen.descriptor) {
        encoded = encoded && writer.write(std::to_integer<std::uint64_t>(value), 8);
    }
    return encoded && writer.write(snapshot.citizen.onlineSessionId, 64);
}

[[nodiscard]] bool write_host_key(encoding::bits::Writer& writer, std::uint64_t key) noexcept {
    for (std::size_t index = 0; index < sizeof key; ++index) {
        if (!writer.write((key >> (index * 8U)) & 0xFFU, 8)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_host_tail(encoding::bits::Writer& writer,
                                   const MembershipSnapshot& snapshot) noexcept {
    // both state fields are 3 bits at bias 1, so logical -1 encodes as wire zero, which is
    // fatal: spawn reader refuses every spawn, teleport reader re-arms each cycle. Send 0
    const auto spawnState = static_cast<std::uint8_t>(
        (snapshot.spawn.state < 0 ? 0 : snapshot.spawn.state) + kSignedFieldBias);
    const auto teleportState = static_cast<std::uint8_t>(
        (snapshot.teleport.state < 0 ? 0 : snapshot.teleport.state) + kSignedFieldBias);
    const auto sliceSetIndex =
        static_cast<std::uint32_t>(snapshot.teleport.sliceSetIndex + kSignedFieldBias);
    return writer.write(snapshot.spawn.opaqueByte, 8) && writer.write(spawnState, kStateBitWidth)
           && writer.write(snapshot.spawn.opaqueValue, 64) && writer.write(0, 4)
           && writer.write(teleportState, kStateBitWidth)
           && writer.write(snapshot.teleport.token, 8)
           && writer.write(sliceSetIndex, kSliceSetBitWidth)
           && writer.write(snapshot.teleport.sliceSetHash, 32) && writer.write(0, 1)
           && writer.write(1, 1) && write_host_key(writer,
                snapshot.hostMemberKey!=0?snapshot.hostMemberKey:snapshot.identity.memberKey)
           && writer.write(0, 1) && writer.write(0, 1);
}

} // namespace

bool write_region_block(encoding::bits::Writer& writer,
                        const MembershipSnapshot& snapshot) noexcept {
    bool encoded = writer.bit_count() == region_block_start_bit(snapshot);
    for (std::size_t bubble = 0; encoded && bubble < kRegionCount; ++bubble) {
        encoded = write_region(writer, bubble, snapshot);
    }
    return encoded && write_host_tail(writer, snapshot)
           && writer.bit_count() == region_block_end_bit(snapshot);
}

} // namespace dawn::middleware::bap::activity_message::replicate_membership
