#include "session_state.h"

#include "../../crypto/lookup3.h"

namespace dawn::middleware::gameplay::group {

namespace {

// absolute byte offsets into the replica; the peer hashes the whole struct, so every one
// must match its layout exactly

constexpr std::size_t kRevisionOffset = 4;
constexpr std::size_t kHostIndexOffset = 12;
constexpr std::size_t kSuccessionOffset = 20;
constexpr std::size_t kMemberCountOffset = 28;
constexpr std::size_t kMemberMaskOffset = 40;
constexpr std::size_t kMemberArrayCountOffset = 48;
constexpr std::size_t kMemberArrayOffset = 56;
constexpr std::size_t kMemberStride = 184;

constexpr std::size_t kMemberAddressLengthOffset = 8;
constexpr std::size_t kMemberAddressOffset = 16;
constexpr std::size_t kMemberMachineLengthOffset = 112;
constexpr std::size_t kMemberMachineOffset = 120;
constexpr std::size_t kMemberJoinIdOffset = 136;
constexpr std::size_t kMemberOwnedPlayerCountOffset = 164;
constexpr std::size_t kMemberPlayerSlotCountOffset = 168;
constexpr std::size_t kMemberPlayerSlotOffset = 176;

constexpr std::size_t kPeerTableOffset = 5968;
constexpr std::size_t kPeerStride = 288;
constexpr std::size_t kPeerStateOffset = 0;
constexpr std::size_t kPeerValueOffset = 4;
constexpr std::size_t kPeerCompatibilityOffset = 8;
constexpr std::size_t kPeerTimestampOffset = 16;

// a bare row sets two words to all ones; a profile apply overwrites them with the
// discriminator and the fixed profile-tail prefix

constexpr std::size_t kPlayerCountOffset = 15184;
constexpr std::size_t kPlayerMaskOffset = 15188;
constexpr std::size_t kPlayerTableOffset = 15192;
constexpr std::size_t kPlayerAdditionNumberOffset = 0x7058;
static_assert(kPlayerAdditionNumberOffset + sizeof(std::uint32_t) <= kSessionStateSize);
constexpr std::size_t kPlayerStride = 424;
constexpr std::size_t kPlayerEmblemDefinitionOffset = 0;
constexpr std::size_t kPlayerIdOffset = 4;
constexpr std::size_t kPlayerMemberOffset = 12;
constexpr std::size_t kPlayerOwnedIndexOffset = 16;
constexpr std::size_t kPlayerSequenceOffset = 20;
constexpr std::size_t kPlayerFlagOffset = 24;
constexpr std::size_t kPlayerClearedFirstOffset = 28;
constexpr std::size_t kPlayerClearedSecondOffset = 264;
constexpr std::size_t kPlayerCodecAOffset = 32;
constexpr std::size_t kPlayerDisplayNameOffset = kPlayerCodecAOffset;
constexpr std::size_t kPlayerIdentityOffset = kPlayerCodecAOffset + 0x80;
constexpr std::size_t kPlayerProfileField2Offset = kPlayerCodecAOffset + 0xB0;
constexpr std::size_t kPlayerProfileField3Offset = kPlayerCodecAOffset + 0xB2;
constexpr std::size_t kPlayerProfileField8Offset = kPlayerCodecAOffset + 0xB3;
constexpr std::size_t kPlayerProfileField4Offset = kPlayerCodecAOffset + 0xB8;
constexpr std::size_t kPlayerProfileField5Offset = kPlayerCodecAOffset + 0xC0;
constexpr std::size_t kPlayerProfileField6Offset = kPlayerCodecAOffset + 0xD0;
constexpr std::size_t kPlayerLightOffset = kPlayerCodecAOffset + 0xE0;
// even with all four Codec-B fields absent, native apply reserves the whole 0x88-byte block
// at Codec-A +0xE8 and stores the fixed tail at +0x170
constexpr std::size_t kPlayerProfileTailOffset = kPlayerCodecAOffset + 0x170;
constexpr std::uint64_t kPlayerCleared = 0xFFFFFFFF;

constexpr std::uint64_t kAddressLength = 86;
constexpr std::uint64_t kMachineIdLength = 8;
constexpr std::uint64_t kOwnedPlayerCount = 1;

// shared seed of the three lookup3 accumulators; a fixed literal, not length-derived
constexpr std::uint32_t kHashInitial = 0xDEAE2F4E;

constexpr unsigned kByteBits = 8;
constexpr std::uint64_t kByteMask = 0xFF;
constexpr std::uint16_t kPlayerNameStateMultiplier = 0x7B4F;
constexpr std::uint32_t kPlayerNameStateMaskSeed = 0xC245B0C4;

[[nodiscard]] constexpr std::uint32_t rotate_left_32(std::uint32_t value,
                                                     unsigned shift) noexcept {
    shift &= 31U;
    return shift == 0 ? value : (value << shift) | (value >> (32U - shift));
}

[[nodiscard]] constexpr std::uint16_t stored_player_name_unit(std::uint16_t wire,
                                                               std::size_t index) noexcept {
    const std::uint16_t mask =
        index == 0
            ? 0
            : static_cast<std::uint16_t>(rotate_left_32(
                  kPlayerNameStateMaskSeed, static_cast<unsigned>(index % 31U)));
    return static_cast<std::uint16_t>(
               static_cast<std::uint32_t>(wire) * kPlayerNameStateMultiplier)
           ^ mask;
}

void write_integer(SessionState& output,
                   std::size_t offset,
                   std::uint64_t value,
                   std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        output[offset + index] = static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
    }
}

} // namespace

void build_session_state(const MembershipUpdate& body, SessionState& output) noexcept {
    output = {};
    const std::uint64_t count = static_cast<std::uint64_t>(body.members.size());
    const std::uint64_t mask = count == 0 ? 0 : (std::uint64_t{1} << count) - 1;

    write_integer(output, kRevisionOffset, body.revision, sizeof(std::uint32_t));
    write_integer(output, kHostIndexOffset, body.hostMemberIndex, sizeof(std::uint32_t));
    write_integer(output, kSuccessionOffset, body.successionIndex, sizeof(std::uint32_t));
    write_integer(output, kMemberCountOffset, count, sizeof(std::uint32_t));
    write_integer(output, kMemberMaskOffset, mask, sizeof(std::uint64_t));
    write_integer(output, kMemberArrayCountOffset, count, sizeof(std::uint64_t));

    for (std::size_t index = 0; index < body.members.size(); ++index) {
        const MembershipMember& member = body.members[index];
        const std::size_t entry = kMemberArrayOffset + kMemberStride * index;
        write_integer(
            output, entry + kMemberAddressLengthOffset, kAddressLength, sizeof(kAddressLength));
        for (std::size_t byte = 0; byte < member.address.size(); ++byte) {
            output[entry + kMemberAddressOffset + byte] = member.address[byte];
        }
        write_integer(
            output, entry + kMemberMachineLengthOffset, kMachineIdLength, sizeof(kMachineIdLength));
        write_integer(
            output, entry + kMemberMachineOffset, member.machineId, sizeof(std::uint64_t));
        write_integer(output, entry + kMemberJoinIdOffset, member.joinId, sizeof(std::uint64_t));
        if (member.ownsPlayerSlot) {
            if (member.ownedPlayerCountPresent) {
                write_integer(output,
                              entry + kMemberOwnedPlayerCountOffset,
                              kOwnedPlayerCount,
                              sizeof(std::uint32_t));
            }
            write_integer(output,
                          entry + kMemberPlayerSlotCountOffset,
                          kOwnedPlayerCount,
                          sizeof(kOwnedPlayerCount));
            write_integer(
                output, entry + kMemberPlayerSlotOffset, member.playerSlot, sizeof(std::uint32_t));
        }

        // the peer clears this table before applying; no connection block leaves zeros
        const std::size_t peer = kPeerTableOffset + kPeerStride * index;
        write_integer(output,
                      peer + kPeerStateOffset,
                      static_cast<std::uint64_t>(member.state),
                      sizeof(std::uint32_t));
        if (member.connectionPresent) {
            write_integer(
                output, peer + kPeerValueOffset, member.connectionValue, sizeof(std::uint32_t));
            write_integer(output,
                          peer + kPeerCompatibilityOffset,
                          member.joinCompatibility,
                          sizeof(std::uint32_t));
            write_integer(
                output, peer + kPeerTimestampOffset, member.joinTimestamp, sizeof(std::uint64_t));
        }
    }

    std::uint64_t playerMask = 0;
    for (const MembershipPlayer& player : body.players) {
        playerMask |= std::uint64_t{1} << player.slot;
        const std::size_t entry = kPlayerTableOffset + kPlayerStride * player.slot;
        write_integer(output, entry + kPlayerIdOffset, player.playerId, sizeof(std::uint64_t));
        write_integer(
            output, entry + kPlayerMemberOffset, player.memberIndex, sizeof(std::uint32_t));
        write_integer(output, entry + kPlayerOwnedIndexOffset, 0, sizeof(std::uint32_t));
        write_integer(
            output, entry + kPlayerSequenceOffset, player.addSequence, sizeof(std::uint32_t));
        write_integer(
            output, entry + kPlayerFlagOffset, player.flag ? 1U : 0U, sizeof(std::uint8_t));
        write_integer(
            output, entry + kPlayerClearedFirstOffset, kPlayerCleared, sizeof(std::uint32_t));
        write_integer(
            output, entry + kPlayerClearedSecondOffset, kPlayerCleared, sizeof(std::uint32_t));
        if (player.profilePresent) {
            write_integer(output,
                          entry + kPlayerEmblemDefinitionOffset,
                          player.emblemDefinitionIndex,
                          sizeof(std::uint32_t));
            write_integer(output,
                          entry + kPlayerClearedFirstOffset,
                          static_cast<std::uint32_t>(player.profileType),
                          sizeof(std::uint32_t));
            // a profile apply copies the fixed tail over the all-ones sentinel; its first word is zero in
            // every captured PC 1.59 ID-34 profile
            write_integer(output, entry + kPlayerClearedSecondOffset, 0U, sizeof(std::uint32_t));
            if (player.displayNamePresent) {
                for (std::size_t unit = 0; unit < player.displayName.size(); ++unit) {
                    const auto wire = static_cast<std::uint16_t>(player.displayName[unit]);
                    write_integer(output,
                                  entry + kPlayerDisplayNameOffset + unit * sizeof(char16_t),
                                  stored_player_name_unit(wire, unit),
                                  sizeof(char16_t));
                    if (wire == 0) {
                        break;
                    }
                }
            }
            if (player.identityPresent) {
                for (std::size_t byte = 0; byte < player.identity.size(); ++byte) {
                    output[entry + kPlayerIdentityOffset + byte] = player.identity[byte];
                }
            }
            const auto& profile = player.profileSupplement;
            if (profile.field2Present) {
                write_integer(output,
                              entry + kPlayerProfileField2Offset,
                              profile.field2,
                              sizeof(std::uint16_t));
            }
            if (profile.field3Present) {
                write_integer(output,
                              entry + kPlayerProfileField3Offset,
                              profile.field3,
                              sizeof(std::uint8_t));
            }
            if (profile.field8Present) {
                write_integer(output,
                              entry + kPlayerProfileField8Offset,
                              profile.field8,
                              sizeof(std::uint8_t));
            }
            if (profile.field4Present) {
                for (std::size_t component = 0; component < profile.field4.size(); ++component) {
                    write_integer(output,
                                  entry + kPlayerProfileField4Offset
                                      + component * sizeof(std::int32_t),
                                  static_cast<std::uint32_t>(profile.field4[component]),
                                  sizeof(std::int32_t));
                }
            }
            if (profile.field5Present) {
                for (std::size_t component = 0; component < profile.field5.size(); ++component) {
                    write_integer(output,
                                  entry + kPlayerProfileField5Offset
                                      + component * sizeof(std::uint64_t),
                                  profile.field5[component],
                                  sizeof(std::uint64_t));
                }
            }
            if (profile.field6Present) {
                write_integer(output,
                              entry + kPlayerProfileField6Offset,
                              profile.field6FloatBits,
                              sizeof(std::uint32_t));
                for (std::size_t component = 0; component < profile.field6Signed.size();
                     ++component) {
                    write_integer(output,
                                  entry + kPlayerProfileField6Offset + sizeof(std::uint32_t)
                                      + component * sizeof(std::int16_t),
                                  static_cast<std::uint16_t>(profile.field6Signed[component]),
                                  sizeof(std::int16_t));
                }
                write_integer(output,
                              entry + kPlayerProfileField6Offset + 0x0C,
                              profile.field6Byte,
                              sizeof(std::uint8_t));
            }
            write_integer(output,
                          entry + kPlayerLightOffset,
                          static_cast<std::uint32_t>(player.light),
                          sizeof(std::uint32_t));
            for (std::size_t word = 0; word < profile.tailWords.size(); ++word) {
                write_integer(output,
                              entry + kPlayerProfileTailOffset
                                  + word * sizeof(std::uint32_t),
                              profile.tailWords[word],
                              sizeof(std::uint32_t));
            }
            write_integer(output,
                          entry + kPlayerProfileTailOffset + 0x0C,
                          profile.tailFive,
                          sizeof(std::uint8_t));
            write_integer(output,
                          entry + kPlayerProfileTailOffset + 0x0D,
                          profile.tailTwo,
                          sizeof(std::uint8_t));
            write_integer(output,
                          entry + kPlayerProfileTailOffset + 0x0E,
                          profile.tailFlag ? 1U : 0U,
                          sizeof(std::uint8_t));
            if (profile.tailOptionalPresent) {
                write_integer(output,
                              entry + kPlayerProfileTailOffset + 0x10,
                              profile.tailOptionalFive,
                              sizeof(std::uint8_t));
            }
        }
    }
    write_integer(output, kPlayerCountOffset, body.players.size(), sizeof(std::uint32_t));
    write_integer(output, kPlayerMaskOffset, playerMask, sizeof(std::uint32_t));
    if (body.playerAdditionNumberPresent) {
        write_integer(output,
                      kPlayerAdditionNumberOffset,
                      body.nextPlayerAdditionNumber,
                      sizeof(std::uint32_t));
    }
}

std::uint32_t session_state_hash(const MembershipUpdate& body) noexcept {
    // the replica is 28 KiB, too large for a stack frame on a game thread
    static thread_local SessionState state{};
    build_session_state(body, state);
    return crypto::lookup3::hash_bytes(state, kHashInitial);
}

} // namespace dawn::middleware::gameplay::group
