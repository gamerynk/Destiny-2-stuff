#include "replicate_membership.h"

#include "../../../core/runtime/runtime_policy.h"

namespace dawn::middleware::bap::activity_message::replicate_membership {
namespace {

constexpr std::uint32_t kLocalMemberMask = 1;

static_assert(kMeaningfulBitCount == kEncodedSize * 8U);

} // namespace

bool encode_replicate_membership(const MembershipSnapshot& snapshot,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    written = 0;
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        return false;
    }
    if (!valid(snapshot)) {
        return false;
    }
    const std::size_t size = encoded_size(snapshot);
    if (output.size() < size) {
        return false;
    }

    encoding::bits::Writer writer(output.first(size));
    // native watcher 1700AB0 enumerates field5 (+5924C), then 4DF640 requires field4 (+59248)
    // before building machine/player updates; occupancy alone cannot create a peer
    const std::uint32_t localMask=kLocalMemberMask<<snapshot.localMemberSlot;
    const std::uint32_t memberMask=occupied_member_mask(snapshot);
    const std::uint32_t joinedMask=localMask|snapshot.joinedMemberMask;
    const bool encoded = writer.write(1, 1) && writer.write(snapshot.revision, 32)
                         && writer.write(snapshot.epoch, 32)
                         && write_member_table(writer, snapshot) && writer.write(1, 1)
                         && write_region_block(writer, snapshot) && writer.write(1, 1)
                         && writer.write(joinedMask, 32) && writer.write(1, 1)
                         && writer.write(memberMask, 32) && writer.write(snapshot.publishPeerConnections?1U:0U, 1)
                         && (!snapshot.publishPeerConnections || writer.write(peer_view_mask(snapshot),32))
                         && writer.write(0, 1) && writer.write(0, 1);
    std::size_t encodedSize = 0;
    const std::size_t meaningfulBits =
        kMeaningfulBitCount
        + (snapshot.citizen.present ? kDescriptorBitCount : 0)
        + member_extra_bits(snapshot)+(snapshot.publishPeerConnections?32U:0U);
    if (!encoded || writer.bit_count() != meaningfulBits || !writer.finish(encodedSize)
        || encodedSize != size) {
        return false;
    }

    written = encodedSize;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::replicate_membership
