#include <bit>
#include <limits>

#include "replicate_membership.h"
#include "activity_player_assignment.h"

namespace dawn::middleware::bap::activity_message::replicate_membership {
namespace {

constexpr std::size_t kMemberKeyByteCount = 8;
constexpr std::size_t kMemberCount = 32;
constexpr std::uint8_t kAbsentMemberBitCount = 3;
// field 1 is a skip test, not a value: the client drops the member when the stored value is
// at or below 0x1FF unsigned, so only the wire value that stores -1 works (wire 1 stores 0)
constexpr std::uint32_t kField1Bias = 1;
constexpr std::uint32_t kField2Bias = 0x80000000U;
constexpr std::uint8_t kLeaveReasonWire = 1;
constexpr std::size_t kIdentityPresenceFieldCount = 15;
constexpr std::uint16_t kPlayerBlobByteCount = player_assignment::kBlobBytes;

[[nodiscard]] bool write_member_key(encoding::bits::Writer& writer, std::uint64_t key) noexcept {
    for (std::size_t index = 0; index < kMemberKeyByteCount; ++index) {
        if (!writer.write((key >> (index * 8U)) & 0xFFU, 8)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_name(encoding::bits::Writer& writer,
                               const PlayerPresentation& player) noexcept {
    const auto length=name_units(player);
    if(!writer.write(length?1U:0U,1)) return false;
    if(!length) return true;
    for(std::size_t i=0;i<=length;++i) if(!writer.write(player.name[i],16)) return false;
    return true;
}
[[nodiscard]] bool write_platform(encoding::bits::Writer& writer,
                                  const PlayerPresentation& player) noexcept {
    if(!writer.write(player.platformId?1U:0U,1)) return false;
    if(!player.platformId) return true;
    // native A49770 serializes a PC platform identity as 36 raw bytes, not its 48-byte struct
    for(unsigned i=0;i<36;++i)
        if(!writer.write(i<8?(player.platformId>>(i*8U))&255U:0U,8)) return false;
    return true;
}

[[nodiscard]] bool write_player_blob(encoding::bits::Writer& writer,
                                     const client_identity::ClientIdentity& identity,
                                     player_assignment::Assignment team,
                                     const PlayerPresentation& player) noexcept {
    const auto start = writer.bit_count();
    bool encoded = writer.write(1, 3) && writer.write(0, 1)
           && write_name(writer,player) && writer.write(1, 1)
           && writer.write(team.faction + player_assignment::kBias,
                           player_assignment::kPlayerWidth)
           && writer.write(0, 2)
           && write_name(writer,player) && write_platform(writer,player)
           && writer.write(0,1) && writer.write(team.publishRequestedTeam?1U:0U,1);
    if (encoded && team.publishRequestedTeam) {
        encoded=writer.write(team.teamIndex+1U,6);
    }
    encoded=encoded && writer.write(0,2) && writer.write(1, 1)
           && writer.write(identity.accountSoid, 64) && writer.write(identity.field5, 64)
           && writer.write(0, 4)
           && writer.write(0, team.publishRequestedTeam?1:player_assignment::kBlobPaddingBits);
    return encoded && writer.bit_count() - start == kPlayerBlobByteCount * 8U+presentation_bits(player);
}

[[nodiscard]] bool write_player_identity(encoding::bits::Writer& writer,
                                         const client_identity::ClientIdentity& identity,
                                         player_assignment::Assignment team,
                                         const PlayerPresentation& player,
                                         const core::protocol::ActivityPeerConnection* peer=nullptr) noexcept {
    for (std::size_t field = 0; field < kIdentityPresenceFieldCount; ++field) {
        const bool present = field == 3 || field == 5 || field == 14
            || (peer && (field==0 || field==10 || field==11 || (field==12 && peer->hasAlternate)));
        if (!writer.write(present ? 1U : 0U, 1)) {
            return false;
        }
        if(peer && field==0 && !writer.write(peer->flags,6)) return false;
        if(peer && present && field>=10 && field<=12){
            const auto& address=field==10?peer->proxy:(field==11?peer->peer:peer->alternate);
            for(const auto element:address) if(!writer.write(std::to_integer<unsigned>(element),8)) return false;
        }
        if (field == 3 && !writer.write(identity.accountSoid, 64)) {
            return false;
        }
        if (field == 5 && !writer.write(identity.field5, 64)) {
            return false;
        }
        if (field == 14
            && (!writer.write(kPlayerBlobByteCount+presentation_bits(player)/8U, 14)
                || !write_player_blob(writer, identity,team,player))) {
            return false;
        }
    }
    return writer.write(0, 1);
}

} // namespace

bool valid(const MembershipSnapshot& snapshot) noexcept {
    for(const auto& player:snapshot.players) if(name_units(player)==player.name.size()) return false;
    for(const auto& peer:snapshot.peerConnections) if(peer.hasFlags && peer.flags>63) return false;
    if (snapshot.localMemberSlot>=32
        || (snapshot.localMemberSlot!=0 && snapshot.reservations[snapshot.localMemberSlot-1].memberKey==0)) return false;
    const auto remoteJoined=snapshot.joinedMemberMask&~(std::uint32_t{1}<<snapshot.localMemberSlot);
    if ((snapshot.joinedMemberMask&~occupied_member_mask(snapshot))!=0
        || (remoteJoined&~snapshot.reservationField3WireWordMask)!=0) return false;
    for (std::size_t i=0;i<snapshot.reservations.size();++i) {
        const auto& row=snapshot.reservations[i];
        if (row.memberKey==0) continue;
        if (row.memberKey==snapshot.identity.memberKey || row.field1!=-1
            || row.accountSoid==0 || row.field5==0) return false;
        for (std::size_t j=0;j<i;++j)
            if (snapshot.reservations[j].memberKey==row.memberKey) return false;
    }
    namespace authoritative = client_authoritative_data;
    const bool sliceSetValid =
        snapshot.teleport.sliceSetIndex >= authoritative::kAbsentSliceSetIndex
        && snapshot.teleport.sliceSetIndex <= authoritative::kMaximumSliceSetIndex;
    const bool hostValid = !snapshot.citizen.present
                           || (snapshot.citizen.memberKey != 0
                               && snapshot.citizen.memberKey != snapshot.identity.memberKey);
    return sliceSetValid && hostValid;
}

bool write_member_table(encoding::bits::Writer& writer,
                        const MembershipSnapshot& snapshot) noexcept {
    const client_identity::ClientIdentity& identity = snapshot.identity;
    const std::uint32_t field1Wire = std::bit_cast<std::uint32_t>(identity.field1) + kField1Bias;
    const std::uint32_t field2Wire = std::bit_cast<std::uint32_t>(identity.field2) + kField2Bias;
    const auto writeLocal=[&]() noexcept {
        return writer.write(1, 1)
                   && write_member_key(writer, identity.memberKey) && writer.write(field1Wire, 10)
                   && writer.write(field2Wire, 32) && writer.write(identity.field3, 64)
                   && writer.write(identity.accountSoid, 64) && writer.write(identity.field5, 64)
                   && writer.write(identity.field6, 64) && writer.write(1, 1) && writer.write(1, 1)
                   && write_player_identity(writer, identity,player_assignment::resolve(
                       snapshot.competitiveTeams,snapshot.teamMode,
                       snapshot.peerConnections[snapshot.localMemberSlot].playerTeam,snapshot.localMemberSlot),
                       snapshot.players[snapshot.localMemberSlot])
                   && writer.write(0, 3)
                   && writer.write(1, 1) && writer.write(kLeaveReasonWire, 5);
    };
    bool encoded=writer.bit_count()==kMemberStartBit;
    for (std::size_t member = 0; encoded && member < kMemberCount; ++member) {
        if (member==snapshot.localMemberSlot) {encoded=writeLocal();continue;}
        const auto source=member==0?snapshot.localMemberSlot:member;
        const auto& row=snapshot.reservations[source-1];
        if (row.memberKey==0) {
            encoded = writer.write(0, kAbsentMemberBitCount);
        } else {
            // reservations stay identity-only; confirmed peers use their validated type23
            // identity in the same minimal detail schema as the local row
            encoded=writer.write(1,1) && write_member_key(writer,row.memberKey)
                && writer.write(std::bit_cast<std::uint32_t>(row.field1)+kField1Bias,10)
                && writer.write(std::bit_cast<std::uint32_t>(row.field2)+kField2Bias,32)
                && ((snapshot.reservationField3WireWordMask&(std::uint32_t{1}<<member))!=0
                    ? writer.write(row.field3,64):write_member_key(writer,row.field3))
                && writer.write(row.accountSoid,64)
                && writer.write(row.field5,64) && writer.write(row.field6,64);
            if ((snapshot.joinedMemberMask&(std::uint32_t{1}<<member))!=0) {
                encoded=encoded && writer.write(1,1) && writer.write(1,1)
                    && write_player_identity(writer,row,player_assignment::resolve(
                        snapshot.competitiveTeams,snapshot.teamMode,snapshot.peerConnections[member].playerTeam,member),snapshot.players[member],
                        publishes_peer_connection(snapshot,member)
                        ? &snapshot.peerConnections[member]:nullptr) && writer.write(0,3)
                    && writer.write(1,1) && writer.write(kLeaveReasonWire,5);
            } else {
                encoded=encoded && writer.write(0,2);
            }
        }
    }
    return encoded && writer.bit_count() + 1 == region_block_start_bit(snapshot);
}

} // namespace dawn::middleware::bap::activity_message::replicate_membership
