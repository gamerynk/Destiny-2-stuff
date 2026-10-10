#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_writer.h"
#include "../../gameplay/descriptor/join_descriptor.h"
#include "activity_client_identity_parser.h"
#include "client_authoritative_data.h"
#include "activity_player_assignment.h"

namespace dawn::middleware::bap::activity_message::replicate_membership {

inline constexpr std::uint32_t kMessageType = 12;
inline constexpr std::size_t kMeaningfulBitCount = 29'984;
inline constexpr std::size_t kEncodedSize = 3'748;
inline constexpr std::size_t kDescriptorBitCount = gameplay::descriptor::kDescriptorSize * 8U;
inline constexpr std::size_t kCitizenEncodedSize = kEncodedSize + kDescriptorBitCount / 8U;

// the client adopts only the record whose region index matches its pending region
struct CitizenAdvertisement final {
    std::array<std::byte, gameplay::descriptor::kDescriptorSize> descriptor{};
    std::uint64_t memberKey{};
    std::array<std::byte, gameplay::descriptor::kNetAddrSize> address{};
    std::uint64_t onlineSessionId{};
    std::int32_t regionIndex{};
    // must differ from the joining client's own slot; an equal slot picks the local-ambassador
    // stage instead of the citizen stage
    std::uint8_t ambassadorSlot{};
    bool present{};
};

// names are UTF-16 on the wire; empty values keep the legacy absent fields, never source
// these from another peer
struct PlayerPresentation final {
    std::array<char16_t,64> name{};
    std::uint64_t platformId{};
};
[[nodiscard]] constexpr std::size_t name_units(const PlayerPresentation& player) noexcept {
    for (std::size_t i=0;i<player.name.size();++i) if(player.name[i]==0) return i;
    return player.name.size();
}
[[nodiscard]] constexpr std::size_t presentation_bits(const PlayerPresentation& player) noexcept {
    const auto length=name_units(player);
    return (length?2*(length+1)*16U:0U)+(player.platformId?36U*8U:0U);
}

struct MembershipSnapshot final {
    bool competitiveTeams{};
    std::array<PlayerPresentation,32> players{};
    player_assignment::Mode teamMode{};
    std::array<core::protocol::ActivityPeerConnection,32> peerConnections{};
    bool publishPeerConnections{};
    client_identity::ClientIdentity identity{};
    std::array<client_identity::ClientIdentity,31> reservations{};
    client_authoritative_data::SpawnState spawn{};
    client_authoritative_data::TeleportState teleport{};
    CitizenAdvertisement citizen{};
    std::uint32_t revision{};
    std::uint32_t epoch{};
    std::uint8_t transitionToken{};
    std::uint8_t localMemberSlot{};
    std::uint64_t hostMemberKey{};
    std::array<std::uint8_t,32> memberTransitionTokens{};
    bool hasMemberTransitionTokens{};
    // the legacy type23 parser keeps field3's wire word; startup uses a low-byte-first key
    std::uint32_t reservationField3WireWordMask{};
    std::uint32_t joinedMemberMask{};
};

[[nodiscard]] constexpr std::size_t reservation_count(const MembershipSnapshot& snapshot) noexcept {
    std::size_t count=0;
    for (const auto& row:snapshot.reservations) count+=row.memberKey!=0?1U:0U;
    return count;
}
inline constexpr std::size_t kReservationIdentityBits=362;
inline constexpr std::size_t kJoinedRemoteDetailBits=327;

[[nodiscard]] constexpr std::uint32_t occupied_member_mask(const MembershipSnapshot& snapshot) noexcept {
    if (snapshot.localMemberSlot>=32) return 0;
    std::uint32_t mask=std::uint32_t{1}<<snapshot.localMemberSlot;
    for (std::size_t i=0;i<snapshot.reservations.size();++i) {
        if (snapshot.reservations[i].memberKey==0) continue;
        const auto slot=i+1==snapshot.localMemberSlot?0:i+1;
        mask|=std::uint32_t{1}<<slot;
    }
    return mask;
}

[[nodiscard]] constexpr std::size_t remote_detail_count(const MembershipSnapshot& snapshot) noexcept {
    const auto local=snapshot.localMemberSlot<32?std::uint32_t{1}<<snapshot.localMemberSlot:0;
    return static_cast<std::size_t>(std::popcount(snapshot.joinedMemberMask&~local));
}

[[nodiscard]] constexpr bool publishes_peer_connection(const MembershipSnapshot& snapshot,
    std::size_t slot) noexcept {
    if(!snapshot.publishPeerConnections || slot>=32 || slot==snapshot.localMemberSlot
        || (snapshot.joinedMemberMask&(std::uint32_t{1}<<slot))==0) return false;
    const auto& peer=snapshot.peerConnections[slot];
    const auto kind=std::to_integer<unsigned>(peer.peer.back());
    if(!peer.hasFlags || !peer.hasPeer || !peer.hasProxy || (kind!=6 && kind!=7)) return false;
    for(std::size_t other=0;other<32;++other){
        if(other!=slot && (snapshot.joinedMemberMask&(std::uint32_t{1}<<other))!=0
            && snapshot.peerConnections[other].hasPeer
            && snapshot.peerConnections[other].peer==peer.peer) return false;
    }
    return true;
}
[[nodiscard]] constexpr std::size_t peer_connection_bits(const MembershipSnapshot& snapshot) noexcept {
    std::size_t count=0;
    for(std::size_t slot=0;slot<32;++slot) if(publishes_peer_connection(snapshot,slot))
        count+=6+688+688+(snapshot.peerConnections[slot].hasAlternate?688:0);
    return count;
}
[[nodiscard]] constexpr std::uint32_t peer_view_mask(const MembershipSnapshot& snapshot) noexcept {
    std::uint32_t mask=0;
    for(std::size_t slot=0;slot<32;++slot)
        if(publishes_peer_connection(snapshot,slot) && (snapshot.peerConnections[slot].flags&0x10)!=0)
            mask|=std::uint32_t{1}<<slot;
    return mask;
}
[[nodiscard]] constexpr std::size_t member_extra_bits(const MembershipSnapshot& snapshot) noexcept {
    std::size_t presentation=0;
    for(std::size_t slot=0;slot<32;++slot)
        if(slot==snapshot.localMemberSlot || (snapshot.joinedMemberMask&(std::uint32_t{1}<<slot)))
            presentation+=presentation_bits(snapshot.players[slot]);
    return presentation+reservation_count(snapshot)*kReservationIdentityBits
        +remote_detail_count(snapshot)*kJoinedRemoteDetailBits+peer_connection_bits(snapshot);
}

[[nodiscard]] constexpr std::size_t encoded_size(const MembershipSnapshot& snapshot) noexcept {
    return ((snapshot.citizen.present ? kCitizenEncodedSize : kEncodedSize)*8U
            +member_extra_bits(snapshot)+(snapshot.publishPeerConnections?32U:0U)+7U)/8U;
}

[[nodiscard]] bool encode_replicate_membership(const MembershipSnapshot& snapshot,
                                               std::span<std::byte> output,
                                               std::size_t& written) noexcept;

inline constexpr std::size_t kMemberStartBit = 65;
inline constexpr std::size_t kRegionBlockStartBit = 851;
inline constexpr std::size_t kRegionBlockEndBit = 29'915;

[[nodiscard]] constexpr std::size_t
region_block_end_bit(const MembershipSnapshot& snapshot) noexcept {
    return kRegionBlockEndBit+(snapshot.citizen.present?kDescriptorBitCount:0U)
        +member_extra_bits(snapshot);
}

[[nodiscard]] constexpr std::size_t
region_block_start_bit(const MembershipSnapshot& snapshot) noexcept {
    return kRegionBlockStartBit+member_extra_bits(snapshot);
}

[[nodiscard]] bool valid(const MembershipSnapshot& snapshot) noexcept;

// the Citizen host exists only in its region record; inserting it into this table
// shifts the captured 3,874-byte body by 1,081 bits
[[nodiscard]] bool write_member_table(encoding::bits::Writer& writer,
                                      const MembershipSnapshot& snapshot) noexcept;

[[nodiscard]] bool write_region_block(encoding::bits::Writer& writer,
                                      const MembershipSnapshot& snapshot) noexcept;

} // namespace dawn::middleware::bap::activity_message::replicate_membership
