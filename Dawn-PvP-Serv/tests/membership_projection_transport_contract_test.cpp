#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>

#include "middleware/social/membership_projection.h"
#include "middleware/social/realtime_codec.h"

namespace {

namespace projection = dawn::middleware::social::membership_projection;
namespace realtime = dawn::middleware::social::realtime;

template <std::size_t Size>
[[nodiscard]] bool load_exact(const char* path,
                              std::array<std::byte, Size>& output) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(Size)) {
        return false;
    }
    input.seekg(0);
    return static_cast<bool>(input.read(reinterpret_cast<char*>(output.data()), Size));
}

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

[[nodiscard]] bool bytes_equal(std::span<const std::byte> left,
                               std::size_t leftOffset,
                               std::span<const std::byte> right,
                               std::size_t rightOffset,
                               std::size_t length) noexcept {
    if (leftOffset > left.size() || length > left.size() - leftOffset
        || rightOffset > right.size() || length > right.size() - rightOffset) {
        return false;
    }
    return std::equal(left.begin() + static_cast<std::ptrdiff_t>(leftOffset),
                      left.begin() + static_cast<std::ptrdiff_t>(leftOffset + length),
                      right.begin() + static_cast<std::ptrdiff_t>(rightOffset));
}

[[nodiscard]] bool decoded_member_row_matches_state(
    std::span<const std::byte> decoded,
    std::span<const std::byte> state,
    std::size_t memberIndex) noexcept {
    constexpr std::array<std::size_t, 7U> presenceOffsets{
        0x00U, 0x68U, 0x80U, 0x90U, 0xA0U, 0xB4U, 0xB6U};
    const std::size_t decodedBase = projection::kDecodedMemberArrayOffset
                                    + memberIndex * projection::kDecodedMemberStride;
    const std::size_t stateBase = projection::kStateMemberArrayOffset
                                  + memberIndex * projection::kStateMemberStride;
    for (std::size_t offset = 0; offset < projection::kStateMemberStride; ++offset) {
        const bool presence = std::find(presenceOffsets.begin(),
                                        presenceOffsets.end(),
                                        offset)
                              != presenceOffsets.end();
        if ((presence && (decoded[decodedBase + offset] != std::byte{1}
                          || state[stateBase + offset] != std::byte{}))
            || (!presence
                && decoded[decodedBase + offset] != state[stateBase + offset])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decoded_member_values_match_state(
    std::span<const std::byte> decoded,
    std::size_t memberIndex,
    std::span<const std::byte> state) noexcept {
    const std::size_t decodedBase = projection::kDecodedMemberArrayOffset
                                    + memberIndex * projection::kDecodedMemberStride;
    const std::size_t stateBase = projection::kStateMemberArrayOffset
                                  + memberIndex * projection::kStateMemberStride;
    return bytes_equal(decoded,
                       decodedBase + projection::kMemberAddressLengthOffset,
                       state,
                       stateBase + projection::kMemberAddressLengthOffset,
                       projection::kMemberAddressAndLengthSize)
           && bytes_equal(decoded,
                          decodedBase + projection::kMemberMachineLengthOffset,
                          state,
                          stateBase + projection::kMemberMachineLengthOffset,
                          projection::kMemberMachineAndLengthSize)
           && bytes_equal(decoded,
                          decodedBase + projection::kMemberJoinIdOffset,
                          state,
                          stateBase + projection::kMemberJoinIdOffset,
                          sizeof(std::uint64_t))
           && bytes_equal(decoded,
                          decodedBase + projection::kMemberOwnedPlayerCountOffset,
                          state,
                          stateBase + projection::kMemberOwnedPlayerCountOffset,
                          sizeof(std::uint32_t))
           && bytes_equal(decoded,
                          decodedBase + projection::kMemberPlayerSlotCountOffset,
                          state,
                          stateBase + projection::kMemberPlayerSlotCountOffset,
                          sizeof(std::uint64_t))
           && bytes_equal(decoded,
                          decodedBase + projection::kMemberPlayerSlotOffset,
                          state,
                          stateBase + projection::kMemberPlayerSlotOffset,
                          sizeof(std::uint32_t));
}

[[nodiscard]] std::size_t find_decoded_peer_row(
    std::span<const std::byte> decoded,
    std::uint32_t memberIndex) noexcept {
    constexpr std::size_t kMaximumMembers = 12U;
    for (std::size_t physicalRow = 0U; physicalRow < kMaximumMembers; ++physicalRow) {
        const std::size_t base = projection::kDecodedPeerArrayOffset
                                 + physicalRow * projection::kDecodedPeerStride;
        if (projection::read_at<std::uint32_t>(
                decoded, base + projection::kDecodedPeerFullOffset)
                == 1U
            && projection::read_at<std::uint32_t>(
                   decoded, base + projection::kDecodedPeerIndexOffset)
                   == memberIndex) {
            return base;
        }
    }
    return decoded.size();
}

[[nodiscard]] std::size_t find_decoded_player_row(
    std::span<const std::byte> decoded,
    std::uint32_t playerIndex) noexcept {
    constexpr std::size_t kMaximumPlayers = 32U;
    for (std::size_t physicalRow = 0U; physicalRow < kMaximumPlayers; ++physicalRow) {
        const std::size_t base = projection::kDecodedPlayerArrayOffset
                                 + physicalRow * projection::kDecodedPlayerStride;
        if (projection::read_at<std::uint32_t>(
                decoded, base + projection::kDecodedPlayerFullOffset)
                == 1U
            && projection::read_at<std::uint8_t>(
                   decoded, base + projection::kDecodedPlayerPresentOffset)
                   == 1U
            && projection::read_at<std::uint32_t>(
                   decoded, base + projection::kDecodedPlayerIndexOffset)
                   == playerIndex) {
            return base;
        }
    }
    return decoded.size();
}

[[nodiscard]] bool decoded_player_matches_state(
    std::span<const std::byte> decoded,
    std::size_t playerSlot,
    std::span<const std::byte> state) noexcept {
    const std::size_t decodedBase = find_decoded_player_row(
        decoded, static_cast<std::uint32_t>(playerSlot));
    const std::size_t stateBase = projection::kStatePlayerArrayOffset
                                  + playerSlot * projection::kStatePlayerStride;
    return decodedBase != decoded.size()
           && projection::read_at<std::uint32_t>(decoded,
                                               decodedBase
                                                   + projection::kDecodedPlayerIndexOffset)
               == playerSlot
           && projection::read_at<std::uint32_t>(decoded,
                                                  decodedBase
                                                      + projection::kDecodedPlayerFullOffset)
                  == 1U
           && projection::read_at<std::uint8_t>(decoded,
                                                 decodedBase
                                                     + projection::kDecodedPlayerPresentOffset)
                  == 1U
           && projection::read_at<std::uint64_t>(decoded,
                                                  decodedBase
                                                      + projection::kDecodedPlayerIdOffset)
                  == projection::read_at<std::uint64_t>(
                      state, stateBase + projection::kStatePlayerIdOffset)
           && projection::read_at<std::uint32_t>(decoded,
                                                  decodedBase
                                                      + projection::kDecodedPlayerMemberOffset)
                  == projection::read_at<std::uint32_t>(
                      state, stateBase + projection::kStatePlayerMemberOffset)
           && projection::read_at<std::uint8_t>(decoded,
                                                 decodedBase
                                                     + projection::kDecodedPlayerOwnedIndexOffset)
                  == projection::read_at<std::uint32_t>(
                      state, stateBase + projection::kStatePlayerOwnedIndexOffset)
           && projection::read_at<std::uint32_t>(decoded,
                                                  decodedBase
                                                      + projection::kDecodedPlayerSequenceOffset)
                  == projection::read_at<std::uint32_t>(
                      state, stateBase + projection::kStatePlayerSequenceOffset)
           && projection::read_at<std::uint8_t>(decoded,
                                                 decodedBase
                                                     + projection::kDecodedPlayerFlagOffset)
                  == projection::read_at<std::uint8_t>(
                      state, stateBase + projection::kStatePlayerFlagOffset)
           && projection::read_at<std::uint8_t>(
                  decoded, decodedBase + projection::kDecodedPlayerProfilePresentOffset)
                  == 1U
           && projection::read_at<std::uint32_t>(decoded,
                                                  decodedBase
                                                      + projection::kDecodedPlayerEmblemOffset)
                  == projection::read_at<std::uint32_t>(
                      state, stateBase + projection::kStatePlayerEmblemOffset)
           && projection::read_at<std::uint32_t>(
                  decoded, decodedBase + projection::kDecodedPlayerProfileTypeOffset)
                  == projection::read_at<std::uint32_t>(
                      state, stateBase + projection::kStatePlayerProfileTypeOffset)
           && bytes_equal(decoded,
                          decodedBase + projection::kDecodedPlayerProfilePayloadOffset,
                          state,
                          stateBase + projection::kStatePlayerProfilePayloadOffset,
                          projection::kStatePlayerProfilePayloadSize);
}

[[nodiscard]] bool state_peer_is_zero_except_state(
    std::span<const std::byte> projected,
    std::size_t memberIndex,
    std::uint32_t expectedState) noexcept {
    const std::size_t base = projection::kStatePeerArrayOffset
                             + memberIndex * projection::kStatePeerStride;
    return projection::read_at<std::uint32_t>(
               projected, base + projection::kStatePeerStateOffset)
               == expectedState
           && std::all_of(
               projected.begin()
                   + static_cast<std::ptrdiff_t>(base + sizeof(std::uint32_t)),
               projected.begin()
                   + static_cast<std::ptrdiff_t>(base + projection::kStatePeerStride),
               [](std::byte value) noexcept { return value == std::byte{}; });
}

template <std::size_t Size>
void clear_bytes(std::array<std::byte, Size>& destination,
                 std::size_t offset,
                 std::size_t length) {
    std::fill_n(destination.begin() + static_cast<std::ptrdiff_t>(offset),
                length,
                std::byte{});
}

void make_distinct_solo_owner(
    const std::array<std::byte, projection::kStateSize>& source,
    std::array<std::byte, projection::kStateSize>& current) {
    constexpr std::uint64_t kOwnerMachineId = 0x0110000100000101ULL;
    constexpr std::uint64_t kOwnerNativePlayerId = 0x0102030405060710ULL;
    constexpr std::uint32_t kOwnerSequence = 7U;
    constexpr std::uint32_t kOwnerEmblem = 0x000004FAU;

    current = source;
    (void)projection::write_at(current, projection::kStateMemberCountOffset, 1U);
    (void)projection::write_at(current, projection::kStateMemberMaskOffset, 1U);
    (void)projection::write_at(current, projection::kStateMemberArrayCountOffset, 1U);
    (void)projection::write_at(current, projection::kStatePlayerCountOffset, 1U);
    (void)projection::write_at(current, projection::kStatePlayerMaskOffset, 1U);
    (void)projection::write_at(current,
                               projection::kStatePlayerAdditionNumberOffset,
                               kOwnerSequence + 1U);

    for (std::size_t member = 1U; member < 12U; ++member) {
        clear_bytes(current,
                    projection::kStateMemberArrayOffset
                        + member * projection::kStateMemberStride,
                    projection::kStateMemberStride);
    }
    for (std::size_t player = 1U; player < 32U; ++player) {
        clear_bytes(current,
                    projection::kStatePlayerArrayOffset
                        + player * projection::kStatePlayerStride,
                    projection::kStatePlayerStride);
    }

    const std::size_t memberBase = projection::kStateMemberArrayOffset;
    (void)projection::write_at(current,
                               memberBase + projection::kMemberOwnedPlayerCountOffset,
                               1U);
    (void)projection::write_at(
        current, memberBase + projection::kMemberMachineOffset, kOwnerMachineId);
    (void)projection::write_at(
        current, memberBase + projection::kMemberJoinIdOffset, std::uint64_t{0U});
    current[memberBase + 0x84U] ^= std::byte{0x6DU};

    const auto seed_peer = [&current](std::size_t member,
                                     std::uint32_t state,
                                     std::uint32_t connectionValue,
                                     std::uint32_t compatibility,
                                     std::uint64_t timestamp,
                                     std::byte opaqueHead,
                                     std::byte opaqueTail) {
        const std::size_t peerBase = projection::kStatePeerArrayOffset
                                     + member * projection::kStatePeerStride;
        (void)projection::write_at(
            current, peerBase + projection::kStatePeerStateOffset, state);
        (void)projection::write_at(
            current,
            peerBase + projection::kStatePeerConnectionValueOffset,
            connectionValue);
        (void)projection::write_at(
            current,
            peerBase + projection::kStatePeerConnectionCompatibilityOffset,
            compatibility);
        (void)projection::write_at(
            current,
            peerBase + projection::kStatePeerConnectionTimestampOffset,
            timestamp);
        current[peerBase + 0x20U] = opaqueHead;
        current[peerBase + projection::kStatePeerStride - 1U] = opaqueTail;
    };
    seed_peer(0U,
              10U,
              0xA11CE001U,
              0xB16B00B5U,
              0x1111222233334444ULL,
              std::byte{0xA5U},
              std::byte{0x5AU});
    seed_peer(1U,
              4U,
              0xA11CE002U,
              0xC011AB1EU,
              0x5555666677778888ULL,
              std::byte{0x3CU},
              std::byte{0xC3U});

    const std::size_t playerBase = projection::kStatePlayerArrayOffset;
    (void)projection::write_at(
        current, playerBase + projection::kStatePlayerIdOffset, kOwnerNativePlayerId);
    (void)projection::write_at(
        current, playerBase + projection::kStatePlayerSequenceOffset, kOwnerSequence);
    (void)projection::write_at(
        current, playerBase + projection::kStatePlayerEmblemOffset, kOwnerEmblem);
    // retail PC reports an unknown native platform account id even with an authenticated Steam
    // identity, so zero is a legitimate recipient value and must be preserved
    (void)projection::write_at(
        current, playerBase + projection::kStatePlayerIdentityOffset, std::uint64_t{0U});
    current[playerBase + projection::kStatePlayerCodecBAssociationOffset] =
        static_cast<std::byte>(projection::kRemoteControllerAssociation);
    current[playerBase + 0x70U] ^= std::byte{0x5AU};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: membership_projection_transport_contract_test decoded state\n",
                   stderr);
        return 2;
    }

    std::array<std::byte, projection::kDecodedSize> capturedDecoded{};
    std::array<std::byte, projection::kStateSize> capturedState{};
    std::array<std::byte, realtime::kEventPayloadCapacity> encoded{};
    std::array<std::byte, projection::kDecodedSize> decodedRoundTrip{};
    std::array<std::byte, projection::kStateSize> stateRoundTrip{};
    bool passed = true;
    passed &= require(load_exact(argv[1], capturedDecoded),
                      "failed-run decoded membership loads exactly");
    passed &= require(load_exact(argv[2], capturedState),
                      "failed-run canonical state loads exactly");

    std::size_t written = 0;
    projection::Metadata metadata{};
    passed &= require(
        projection::compress(capturedDecoded, capturedState, encoded, written, &metadata),
        "real two-player native projection fits one realtime payload");
    passed &= require(written > projection::kHeaderSize
                          && written <= realtime::kEventPayloadCapacity,
                      "encoded projection respects the 4096-byte event boundary");
    passed &= require(metadata.revision == 1 && metadata.baseRevision == 0
                          && metadata.memberCount == 2 && metadata.playerCount == 2
                          && metadata.expectedHash == 0x7548903FU,
                      "transport metadata preserves the failed-run native revision domain");

    projection::Metadata decodedMetadata{};
    passed &= require(
        projection::decompress(std::span(encoded).first(written),
                               decodedRoundTrip,
                               stateRoundTrip,
                               &decodedMetadata),
        "encoded projection decodes under strict run validation");
    passed &= require(decodedRoundTrip == capturedDecoded
                          && stateRoundTrip == capturedState,
                      "transport preserves every decoded and canonical-state byte");
    passed &= require(decodedMetadata.revision == metadata.revision
                          && decodedMetadata.expectedHash == metadata.expectedHash
                          && decodedMetadata.memberCount == metadata.memberCount
                          && decodedMetadata.playerCount == metadata.playerCount,
                      "decoded metadata is exact");

    if (written > projection::kHeaderSize) {
        const std::byte retained = encoded[written - 1];
        encoded[written - 1] = std::byte{};
        passed &= require(
            !projection::decompress(std::span(encoded).first(written),
                                    decodedRoundTrip,
                                    stateRoundTrip),
            "zero-byte corruption inside a nonzero run is rejected");
        encoded[written - 1] = retained;
    }
    passed &= require(
        !projection::decompress(std::span(encoded).first(written - 1),
                                decodedRoundTrip,
                                stateRoundTrip),
        "truncated projection is rejected");

    constexpr std::uint64_t kOwnerPlatformId = 0x0110000100000101ULL;
    constexpr std::uint64_t kRemotePlatformId = 0x0110000100000103ULL;
    std::array<std::byte, projection::kDecodedSize> rebasedDecoded = capturedDecoded;
    std::array<std::byte, projection::kStateSize> rebasedState = capturedState;
    std::array<std::byte, projection::kStateSize> currentOwnerState{};
    make_distinct_solo_owner(capturedState, currentOwnerState);
    (void)projection::write_at(rebasedDecoded,
                               projection::kPlayerAdditionNumberPresentOffset,
                               std::uint8_t{1U});
    (void)projection::write_at(rebasedDecoded,
                               projection::kPlayerAdditionNumberOffset,
                               std::uint32_t{2U});
    (void)projection::write_at(rebasedState,
                               projection::kStatePlayerAdditionNumberOffset,
                               std::uint32_t{2U});
    for (std::size_t member = 0U; member < 2U; ++member) {
        (void)projection::write_at(
            rebasedDecoded,
            projection::kDecodedMemberArrayOffset
                + member * projection::kDecodedMemberStride
                + projection::kMemberOwnedPlayerCountOffset,
            std::uint32_t{1U});
        (void)projection::write_at(
            rebasedState,
            projection::kStateMemberArrayOffset
                + member * projection::kStateMemberStride
                + projection::kMemberOwnedPlayerCountOffset,
            std::uint32_t{1U});
    }
    std::swap_ranges(
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPlayerArrayOffset),
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPlayerArrayOffset
                                          + projection::kDecodedPlayerStride),
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPlayerArrayOffset
                                          + projection::kDecodedPlayerStride));
    std::swap_ranges(
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPeerArrayOffset),
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPeerArrayOffset
                                          + projection::kDecodedPeerStride),
        rebasedDecoded.begin()
            + static_cast<std::ptrdiff_t>(projection::kDecodedPeerArrayOffset
                                          + projection::kDecodedPeerStride));
    for (std::size_t slot = 0U; slot < 2U; ++slot) {
        const std::uint8_t association =
            slot == 0U ? projection::kRemoteControllerAssociation
                       : projection::kLocalControllerAssociation;
        rebasedState[projection::kStatePlayerArrayOffset
                       + slot * projection::kStatePlayerStride
                       + projection::kStatePlayerCodecBAssociationOffset] =
            static_cast<std::byte>(association);
        const std::size_t decodedPlayer = find_decoded_player_row(
            rebasedDecoded, static_cast<std::uint32_t>(slot));
        if (decodedPlayer != rebasedDecoded.size()) {
            rebasedDecoded[decodedPlayer
                           + projection::kDecodedPlayerCodecBAssociationOffset] =
                static_cast<std::byte>(association);
        }
    }
    rebasedState[projection::kStateMemberArrayOffset
                   + 7U * projection::kStateMemberStride + 0x31U] = std::byte{0xA7};
    rebasedState[projection::kStatePeerArrayOffset
                   + 9U * projection::kStatePeerStride + 0x81U] = std::byte{0xB9};
    rebasedState[projection::kStatePlayerArrayOffset
                   + 11U * projection::kStatePlayerStride + 0x101U] = std::byte{0xCB};
    currentOwnerState[projection::kStateMemberArrayOffset
                        + 7U * projection::kStateMemberStride + 0x31U] = std::byte{0xE7};
    currentOwnerState[projection::kStatePeerArrayOffset
                        + 9U * projection::kStatePeerStride + 0x81U] = std::byte{0xE9};
    currentOwnerState[projection::kStatePlayerArrayOffset
                        + 11U * projection::kStatePlayerStride + 0x101U] = std::byte{0xEB};
    const auto beforeRebaseDecoded = rebasedDecoded;
    const auto beforeRebaseState = rebasedState;

    projection::OwnerRebaseSummary summary{};
    const char* rebaseFailure = "none";
    const bool rebasePassed = projection::rebase_solo_owner(rebasedDecoded,
                                                             rebasedState,
                                                             currentOwnerState,
                                                             kOwnerPlatformId,
                                                             &summary,
                                                             kRemotePlatformId,
                                                             &rebaseFailure);
    if (!rebasePassed) {
        std::fprintf(stderr, "INFO: initial rebase failure stage: %s\n", rebaseFailure);
    }
    passed &= require(
        rebasePassed,
        "failed-run two-player projection rebases onto the passive owner's solo state");

    const std::size_t ownerMemberState = projection::kStateMemberArrayOffset;
    const std::size_t remoteMemberState = ownerMemberState + projection::kStateMemberStride;
    const std::size_t remoteMemberDecoded = projection::kDecodedMemberArrayOffset
                                            + projection::kDecodedMemberStride;
    const std::size_t ownerPlayerSlot = projection::read_at<std::uint32_t>(
        currentOwnerState, ownerMemberState + projection::kMemberPlayerSlotOffset);
    const std::size_t remotePlayerSlot = projection::read_at<std::uint32_t>(
        beforeRebaseState, remoteMemberState + projection::kMemberPlayerSlotOffset);
    const std::size_t ownerPlayerState = projection::kStatePlayerArrayOffset
                                         + ownerPlayerSlot * projection::kStatePlayerStride;
    const std::size_t remotePlayerState = projection::kStatePlayerArrayOffset
                                          + remotePlayerSlot * projection::kStatePlayerStride;
    const std::size_t ownerPlayerDecodedBefore = find_decoded_player_row(
        beforeRebaseDecoded, static_cast<std::uint32_t>(ownerPlayerSlot));
    const std::size_t remotePlayerDecodedBefore = find_decoded_player_row(
        beforeRebaseDecoded, static_cast<std::uint32_t>(remotePlayerSlot));
    const std::size_t remotePlayerDecodedAfter = find_decoded_player_row(
        rebasedDecoded, static_cast<std::uint32_t>(remotePlayerSlot));
    const std::size_t ownerPlayerDecodedAfter = find_decoded_player_row(
        rebasedDecoded, static_cast<std::uint32_t>(ownerPlayerSlot));

    passed &= require(
        ownerPlayerDecodedBefore
                == projection::kDecodedPlayerArrayOffset
                       + projection::kDecodedPlayerStride
            && remotePlayerDecodedBefore == projection::kDecodedPlayerArrayOffset,
        "fixture presents player deltas out of slot order to exercise index-based lookup");

    const std::size_t ownerPeerDecodedBefore = find_decoded_peer_row(
        beforeRebaseDecoded, 0U);
    const std::size_t remotePeerDecodedBefore = find_decoded_peer_row(
        beforeRebaseDecoded, 1U);
    const std::size_t ownerPeerDecodedAfter = find_decoded_peer_row(rebasedDecoded, 0U);
    const std::size_t remotePeerDecodedAfter = find_decoded_peer_row(rebasedDecoded, 1U);
    passed &= require(
        ownerPeerDecodedBefore
                == projection::kDecodedPeerArrayOffset + projection::kDecodedPeerStride
            && remotePeerDecodedBefore == projection::kDecodedPeerArrayOffset
            && ownerPeerDecodedAfter == rebasedDecoded.size()
            && remotePeerDecodedAfter == projection::kDecodedPeerArrayOffset,
        "recipient delta keeps only the remote peer and compacts it by native index");

    passed &= require(
        bytes_equal(rebasedState,
                    ownerMemberState,
                    currentOwnerState,
                    ownerMemberState,
                    projection::kStateMemberStride),
        "owner canonical member incarnation is preserved exactly");
    passed &= require(
        decoded_member_values_match_state(rebasedDecoded, 0U, currentOwnerState),
        "owner decoded member values match the preserved canonical member");
    passed &= require(
        decoded_member_row_matches_state(rebasedDecoded, currentOwnerState, 0U),
        "owner decoded row is rebuilt from canonical bytes with valid presence markers");
    passed &= require(
        bytes_equal(rebasedState,
                    ownerPlayerState,
                    currentOwnerState,
                    ownerPlayerState,
                    projection::kStatePlayerStride)
            && ownerPlayerDecodedAfter == rebasedDecoded.size(),
        "owner canonical player is preserved and omitted from the remote-add delta");

    passed &= require(
        bytes_equal(rebasedState,
                    remoteMemberState,
                    beforeRebaseState,
                    remoteMemberState,
                    projection::kStateMemberStride)
            && bytes_equal(rebasedDecoded,
                           remoteMemberDecoded,
                           beforeRebaseDecoded,
                           remoteMemberDecoded,
                           projection::kDecodedMemberStride)
            && bytes_equal(rebasedState,
                           remotePlayerState + projection::kStatePlayerProfilePayloadOffset,
                           beforeRebaseState,
                           remotePlayerState + projection::kStatePlayerProfilePayloadOffset,
                           projection::kStatePlayerCodecBAssociationOffset
                               - projection::kStatePlayerProfilePayloadOffset)
            && bytes_equal(rebasedState,
                           remotePlayerState
                               + projection::kStatePlayerCodecBAssociationOffset + 1U,
                           beforeRebaseState,
                           remotePlayerState
                               + projection::kStatePlayerCodecBAssociationOffset + 1U,
                           projection::kStatePlayerStride
                               - projection::kStatePlayerCodecBAssociationOffset - 1U)
            && projection::read_at<std::uint8_t>(
                   rebasedState,
                   remotePlayerState + projection::kStatePlayerCodecBAssociationOffset)
                   == projection::kLocalControllerAssociation
            && decoded_player_matches_state(rebasedDecoded,
                                            remotePlayerSlot,
                                            rebasedState)
            && remotePlayerDecodedAfter
                   == projection::kDecodedPlayerArrayOffset,
        "remote member/player retain the transported recipient association in the stable delta");

    const bool remoteConnectionCleared =
        projection::read_at<std::uint8_t>(
            beforeRebaseDecoded,
            remotePeerDecodedBefore + projection::kDecodedPeerConnectionPresentOffset)
            == 1U
        && std::all_of(
            rebasedDecoded.begin()
                + static_cast<std::ptrdiff_t>(
                    remotePeerDecodedAfter + projection::kDecodedPeerConnectionPresentOffset),
            rebasedDecoded.begin()
                + static_cast<std::ptrdiff_t>(
                    remotePeerDecodedAfter + projection::kDecodedPeerStride),
            [](std::byte value) noexcept { return value == std::byte{}; });
    passed &= require(remoteConnectionCleared,
                      "uploader-local remote connection payload is cleared from the owner delta");
    const std::uint32_t ownerCurrentRevision = projection::read_at<std::uint32_t>(
        currentOwnerState, projection::kStateRevisionOffset);
    passed &= require(
        projection::read_at<std::int32_t>(beforeRebaseDecoded,
                                          projection::kBaseRevisionOffset)
                == 0
            && projection::read_at<std::int32_t>(rebasedDecoded,
                                                 projection::kBaseRevisionOffset)
                   == static_cast<std::int32_t>(ownerCurrentRevision)
            && projection::read_at<std::uint16_t>(rebasedDecoded,
                                                  projection::kMemberCountOffset)
                   == 1U
            && projection::read_at<std::uint16_t>(rebasedDecoded,
                                                  projection::kPlayerCountOffset)
                   == 1U,
        "owner projection is an exact-revision one-peer/one-player incremental delta");
    const std::uint32_t remoteProjectedPeerState = projection::read_at<std::uint32_t>(
        beforeRebaseState,
        projection::kStatePeerArrayOffset + projection::kStatePeerStride
            + projection::kStatePeerStateOffset);
    passed &= require(
        bytes_equal(rebasedState,
                    projection::kStatePeerArrayOffset,
                    currentOwnerState,
                    projection::kStatePeerArrayOffset,
                    projection::kStatePeerStride)
            && state_peer_is_zero_except_state(rebasedState, 1U, remoteProjectedPeerState),
        "owner peer row remains exact while the remote delta publishes state without P2P data");
    passed &= require(
        bytes_equal(rebasedState,
                    projection::kStateMemberArrayOffset
                        + 2U * projection::kStateMemberStride,
                    currentOwnerState,
                    projection::kStateMemberArrayOffset
                        + 2U * projection::kStateMemberStride,
                    (projection::kNativeStateMemberCapacity - 2U)
                        * projection::kStateMemberStride)
            && bytes_equal(rebasedState,
                           projection::kStatePeerArrayOffset
                               + 2U * projection::kStatePeerStride,
                           currentOwnerState,
                           projection::kStatePeerArrayOffset
                               + 2U * projection::kStatePeerStride,
                           (projection::kNativeStateMemberCapacity - 2U)
                               * projection::kStatePeerStride)
            && bytes_equal(rebasedState,
                           projection::kStatePlayerArrayOffset
                               + 2U * projection::kStatePlayerStride,
                           currentOwnerState,
                           projection::kStatePlayerArrayOffset
                               + 2U * projection::kStatePlayerStride,
                           (projection::kNativePlayerCapacity - 2U)
                               * projection::kStatePlayerStride),
        "incremental projection preserves every inactive owner-recipient table byte");
    passed &= require(
        ownerPeerDecodedAfter == rebasedDecoded.size()
            && projection::read_at<std::uint32_t>(
                   rebasedState,
                   projection::kStatePeerArrayOffset + projection::kStatePeerStateOffset)
                   == projection::read_at<std::uint32_t>(
                       currentOwnerState,
                       projection::kStatePeerArrayOffset
                           + projection::kStatePeerStateOffset)
            && projection::read_at<std::uint32_t>(
                   rebasedDecoded,
                   remotePeerDecodedAfter + projection::kDecodedPeerStateOffset)
                   == projection::read_at<std::uint32_t>(
                       beforeRebaseDecoded,
                       remotePeerDecodedBefore + projection::kDecodedPeerStateOffset)
            && projection::read_at<std::uint32_t>(
                   rebasedState,
                   projection::kStatePeerArrayOffset + projection::kStatePeerStride
                       + projection::kStatePeerStateOffset)
                   == projection::read_at<std::uint32_t>(
                       beforeRebaseState,
                       projection::kStatePeerArrayOffset + projection::kStatePeerStride
                           + projection::kStatePeerStateOffset),
        "owner peer has no delta while the remote peer state remains projected");

    const std::uint64_t remoteMachineId = projection::read_at<std::uint64_t>(
        beforeRebaseState, remoteMemberState + projection::kMemberMachineOffset);
    const std::uint64_t remoteJoinId = projection::read_at<std::uint64_t>(
        beforeRebaseState, remoteMemberState + projection::kMemberJoinIdOffset);
    const std::uint64_t remotePlayerId = projection::read_at<std::uint64_t>(
        beforeRebaseState, remotePlayerState + projection::kStatePlayerIdOffset);
    const std::uint32_t remoteSequence = projection::read_at<std::uint32_t>(
        beforeRebaseState, remotePlayerState + projection::kStatePlayerSequenceOffset);
    passed &= require(
        summary.ownerMachineId
                == projection::read_at<std::uint64_t>(
                    currentOwnerState,
                    ownerMemberState + projection::kMemberMachineOffset)
            && summary.ownerJoinId
                   == projection::read_at<std::uint64_t>(
                       currentOwnerState,
                       ownerMemberState + projection::kMemberJoinIdOffset)
            && summary.ownerPlayerId
                   == projection::read_at<std::uint64_t>(
                       currentOwnerState,
                       ownerPlayerState + projection::kStatePlayerIdOffset)
            && summary.ownerPlayerSlot == ownerPlayerSlot
            && summary.ownerPlayerSequence
                   == projection::read_at<std::uint32_t>(
                       currentOwnerState,
                       ownerPlayerState + projection::kStatePlayerSequenceOffset)
            && summary.remoteMachineId == remoteMachineId
            && summary.remoteJoinId == remoteJoinId
            && summary.remotePlayerId == remotePlayerId
            && summary.remotePlatformId == kRemotePlatformId
            && summary.remotePlayerSequence
                   == projection::read_at<std::uint32_t>(
                       currentOwnerState,
                       projection::kStatePlayerAdditionNumberOffset)
            && summary.clearedRemoteConnections == 1U,
        "rebase summary exposes distinct owner and remote native row keys");

    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           currentOwnerState,
                                           kOwnerPlatformId + 1U),
            "wrong authenticated owner platform is rejected");
    }
    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        projection::OwnerRebaseSummary ignored{};
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           currentOwnerState,
                                           kOwnerPlatformId,
                                           &ignored,
                                           kRemotePlatformId + 1U),
            "projection uploader must match one authenticated remote platform row");
    }
    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        auto invalidCurrent = currentOwnerState;
        (void)projection::write_at(invalidCurrent,
                                   projection::kStateMemberCountOffset,
                                   2U);
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           invalidCurrent,
                                           kOwnerPlatformId),
            "non-solo current owner membership is rejected");
    }
    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        auto invalidCurrent = currentOwnerState;
        (void)projection::write_at(invalidCurrent,
                                   ownerMemberState + projection::kMemberJoinIdOffset,
                                   remoteJoinId);
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           invalidCurrent,
                                           kOwnerPlatformId),
            "duplicate owner and remote join row keys are rejected");
    }
    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        auto invalidCurrent = currentOwnerState;
        (void)projection::write_at(invalidCurrent,
                                   ownerPlayerState + projection::kStatePlayerIdOffset,
                                   remotePlayerId);
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           invalidCurrent,
                                           kOwnerPlatformId),
            "duplicate owner and remote native player row keys are rejected");
    }
    {
        auto decoded = capturedDecoded;
        auto state = capturedState;
        auto invalidCurrent = currentOwnerState;
        (void)projection::write_at(invalidCurrent,
                                   ownerPlayerState + projection::kStatePlayerSequenceOffset,
                                   remoteSequence);
        passed &= require(
            !projection::rebase_solo_owner(decoded,
                                           state,
                                           invalidCurrent,
                                           kOwnerPlatformId),
            "duplicate owner and remote add sequences are rejected");
    }

    if (!passed) {
        return 1;
    }
    std::printf("PASS: SMP2 transport and passive-owner incremental delta (%zu bytes)\n",
                written);
    return 0;
}
