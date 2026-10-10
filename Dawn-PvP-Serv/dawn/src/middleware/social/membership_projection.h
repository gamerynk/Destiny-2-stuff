#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace dawn::middleware::social::membership_projection {

// v2 carries the decoded ID-30 object and the retail client's canonical state; the owner must
// rebase it onto its own member-zero record, or the owner's self address gets removed
inline constexpr std::uint32_t kMagic = 0x32504D53U;
inline constexpr std::uint16_t kVersion = 2U;
inline constexpr std::size_t kHeaderSize = 48U;
inline constexpr std::size_t kRunHeaderSize = 4U;
inline constexpr std::size_t kDecodedSize = 0x7980U;
inline constexpr std::size_t kStateSize = 0x7060U;
inline constexpr std::size_t kRevisionOffset = 0x0CU;
inline constexpr std::size_t kBaseRevisionOffset = 0x1740U;
inline constexpr std::size_t kMemberCountOffset = 0x1748U;
inline constexpr std::size_t kPlayerCountOffset = 0x174AU;
inline constexpr std::size_t kPlayerAdditionNumberPresentOffset = 0x7950U;
inline constexpr std::size_t kPlayerAdditionNumberOffset = 0x7954U;
inline constexpr std::size_t kHashOffset = 0x7978U;
inline constexpr std::size_t kStateRevisionOffset = 0x04U;
inline constexpr std::size_t kStateMemberCountOffset = 0x1CU;
inline constexpr std::size_t kStatePlayerCountOffset = 0x3B50U;

// native PC 1.59 layouts for the recipient rebase. Decoded ID-30 rows hold presence markers;
// copying a state row wholesale into them clears those markers and is never valid
inline constexpr std::size_t kDecodedMemberArrayOffset = 0x40U;
inline constexpr std::size_t kDecodedMemberStride = 0xB8U;
inline constexpr std::size_t kDecodedPeerArrayOffset = 0x1750U;
inline constexpr std::size_t kDecodedPeerStride = 0x158U;
inline constexpr std::size_t kDecodedPlayerArrayOffset = 0x4250U;
inline constexpr std::size_t kDecodedPlayerStride = 0x1B8U;

inline constexpr std::size_t kStateHostIndexOffset = 0x0CU;
inline constexpr std::size_t kStateSuccessionIndexOffset = 0x14U;
inline constexpr std::size_t kStateMemberMaskOffset = 0x28U;
inline constexpr std::size_t kStateMemberArrayCountOffset = 0x30U;
inline constexpr std::size_t kStateMemberArrayOffset = 0x38U;
inline constexpr std::size_t kStateMemberStride = 0xB8U;
inline constexpr std::size_t kStatePeerArrayOffset = 0x1750U;
inline constexpr std::size_t kStatePeerStride = 0x120U;
inline constexpr std::size_t kStatePlayerMaskOffset = 0x3B54U;
inline constexpr std::size_t kStatePlayerArrayOffset = 0x3B58U;
inline constexpr std::size_t kStatePlayerStride = 0x1A8U;
inline constexpr std::size_t kStatePlayerAdditionNumberOffset = 0x7058U;

inline constexpr std::size_t kMemberAddressLengthOffset = 0x08U;
inline constexpr std::size_t kMemberAddressAndLengthSize = 0x60U;
inline constexpr std::size_t kMemberMachineLengthOffset = 0x70U;
inline constexpr std::size_t kMemberMachineAndLengthSize = 0x10U;
inline constexpr std::size_t kMemberMachineOffset = 0x78U;
inline constexpr std::size_t kMemberJoinIdOffset = 0x88U;
inline constexpr std::size_t kMemberOwnedPlayerCountOffset = 0xA4U;
inline constexpr std::size_t kMemberPlayerSlotCountOffset = 0xA8U;
inline constexpr std::size_t kMemberPlayerSlotOffset = 0xB0U;

inline constexpr std::size_t kDecodedPeerIndexOffset = 0x00U;
inline constexpr std::size_t kDecodedPeerFullOffset = 0x04U;
inline constexpr std::size_t kDecodedPeerStateOffset = 0x08U;
inline constexpr std::size_t kDecodedPeerConnectionPresentOffset = 0x0CU;
inline constexpr std::size_t kDecodedPeerConnectionValueOffset = 0x10U;
inline constexpr std::size_t kDecodedPeerConnectionTimestampOffset = 0x18U;
inline constexpr std::size_t kDecodedPeerConnectionCompatibilityOffset = 0x20U;
inline constexpr std::size_t kStatePeerStateOffset = 0x00U;
inline constexpr std::size_t kStatePeerConnectionValueOffset = 0x04U;
inline constexpr std::size_t kStatePeerConnectionCompatibilityOffset = 0x08U;
inline constexpr std::size_t kStatePeerConnectionTimestampOffset = 0x10U;

inline constexpr std::size_t kDecodedPlayerIndexOffset = 0x00U;
inline constexpr std::size_t kDecodedPlayerFullOffset = 0x04U;
inline constexpr std::size_t kDecodedPlayerPresentOffset = 0x08U;
inline constexpr std::size_t kDecodedPlayerIdOffset = 0x09U;
inline constexpr std::size_t kDecodedPlayerMemberOffset = 0x14U;
inline constexpr std::size_t kDecodedPlayerOwnedIndexOffset = 0x18U;
inline constexpr std::size_t kDecodedPlayerSequenceOffset = 0x1CU;
inline constexpr std::size_t kDecodedPlayerFlagOffset = 0x20U;
inline constexpr std::size_t kDecodedPlayerProfilePresentOffset = 0x21U;
inline constexpr std::size_t kDecodedPlayerEmblemOffset = 0x24U;
inline constexpr std::size_t kDecodedPlayerProfileTypeOffset = 0x28U;
inline constexpr std::size_t kDecodedPlayerProfilePayloadOffset = 0x30U;
inline constexpr std::size_t kStatePlayerEmblemOffset = 0x00U;
inline constexpr std::size_t kStatePlayerIdOffset = 0x04U;
inline constexpr std::size_t kStatePlayerMemberOffset = 0x0CU;
inline constexpr std::size_t kStatePlayerOwnedIndexOffset = 0x10U;
inline constexpr std::size_t kStatePlayerSequenceOffset = 0x14U;
inline constexpr std::size_t kStatePlayerFlagOffset = 0x18U;
inline constexpr std::size_t kStatePlayerProfileTypeOffset = 0x1CU;
inline constexpr std::size_t kStatePlayerProfilePayloadOffset = 0x20U;
inline constexpr std::size_t kStatePlayerIdentityOffset = 0xA0U;
inline constexpr std::size_t kStatePlayerProfilePayloadSize =
    kStatePlayerStride - kStatePlayerProfilePayloadOffset;

inline constexpr std::uint64_t kNativeMemberAddressLength = 86U;
inline constexpr std::uint64_t kNativeMemberMachineLength = 8U;
inline constexpr std::uint32_t kBarePlayerProfileSentinel = 0xFFFFFFFFU;
inline constexpr std::uint32_t kPlayerAdditionNumberMask = 0x000FFFFFU;
inline constexpr std::size_t kNativeMemberCapacity = 12U;
inline constexpr std::size_t kNativePlayerCapacity = 32U;
inline constexpr std::size_t kNativeStateMemberCapacity = 32U;

static_assert(kStateMemberArrayOffset
                  + kNativeStateMemberCapacity * kStateMemberStride
              <= kStatePeerArrayOffset);
static_assert(kStatePeerArrayOffset + kNativeStateMemberCapacity * kStatePeerStride
              <= kStatePlayerCountOffset);
static_assert(kStatePlayerArrayOffset + kNativePlayerCapacity * kStatePlayerStride
              <= kStateSize);
static_assert(kStatePlayerAdditionNumberOffset + sizeof(std::uint32_t) <= kStateSize);
static_assert(kPlayerAdditionNumberPresentOffset + sizeof(std::uint8_t) <= kDecodedSize);
static_assert(kPlayerAdditionNumberOffset + sizeof(std::uint32_t) <= kDecodedSize);

struct OwnerRebaseSummary final {
    std::uint64_t ownerMachineId{};
    std::uint64_t ownerJoinId{};
    std::uint64_t ownerPlayerId{};
    std::uint32_t ownerPlayerSlot{};
    std::uint32_t ownerPlayerSequence{};
    std::uint64_t remoteMachineId{};
    std::uint64_t remoteJoinId{};
    std::uint64_t remotePlayerId{};
    std::uint64_t remotePlatformId{};
    std::uint32_t remotePlayerSequence{};
    std::uint32_t priorPlayerAdditionNumber{};
    std::uint32_t nextPlayerAdditionNumber{};
    std::uint32_t clearedRemoteConnections{};
};

struct Metadata final {
    std::uint32_t revision{};
    std::int32_t baseRevision{};
    std::uint32_t expectedHash{};
    std::uint16_t memberCount{};
    std::uint16_t playerCount{};
    std::uint16_t decodedRunCount{};
    std::uint32_t stateHash{};
    std::uint16_t stateRunCount{};
};

template <typename Value>
[[nodiscard]] inline Value read_at(std::span<const std::byte> source,
                                   std::size_t offset) noexcept {
    Value value{};
    if (offset <= source.size() && sizeof(value) <= source.size() - offset) {
        std::memcpy(&value, source.data() + offset, sizeof(value));
    }
    return value;
}

template <typename Value>
[[nodiscard]] inline bool write_at(std::span<std::byte> destination,
                                   std::size_t offset,
                                   Value value) noexcept {
    if (offset > destination.size() || sizeof(value) > destination.size() - offset) {
        return false;
    }
    std::memcpy(destination.data() + offset, &value, sizeof(value));
    return true;
}

[[nodiscard]] inline bool valid_metadata(const Metadata& metadata) noexcept {
    return metadata.revision != 0U && metadata.baseRevision == 0
           && metadata.expectedHash != 0U && metadata.stateHash == metadata.expectedHash
           && metadata.memberCount >= 2U && metadata.memberCount <= 12U
           && metadata.playerCount >= 2U && metadata.playerCount <= metadata.memberCount
           && metadata.decodedRunCount != 0U && metadata.stateRunCount != 0U;
}

[[nodiscard]] inline bool inspect(std::span<const std::byte> encoded,
                                  Metadata& metadata) noexcept {
    metadata = {};
    if (encoded.size() < kHeaderSize || read_at<std::uint32_t>(encoded, 0U) != kMagic
        || read_at<std::uint16_t>(encoded, 4U) != kVersion
        || read_at<std::uint16_t>(encoded, 6U) != kHeaderSize
        || read_at<std::uint32_t>(encoded, 8U) != kDecodedSize
        || read_at<std::uint16_t>(encoded, 30U) != 0U
        || read_at<std::uint32_t>(encoded, 32U) != kStateSize
        || read_at<std::uint16_t>(encoded, 42U) != 0U
        || read_at<std::uint32_t>(encoded, 44U) != 0U) {
        return false;
    }
    metadata.revision = read_at<std::uint32_t>(encoded, 12U);
    metadata.baseRevision = read_at<std::int32_t>(encoded, 16U);
    metadata.expectedHash = read_at<std::uint32_t>(encoded, 20U);
    metadata.memberCount = read_at<std::uint16_t>(encoded, 24U);
    metadata.playerCount = read_at<std::uint16_t>(encoded, 26U);
    metadata.decodedRunCount = read_at<std::uint16_t>(encoded, 28U);
    metadata.stateHash = read_at<std::uint32_t>(encoded, 36U);
    metadata.stateRunCount = read_at<std::uint16_t>(encoded, 40U);
    const std::size_t runCount = static_cast<std::size_t>(metadata.decodedRunCount)
                                 + metadata.stateRunCount;
    return valid_metadata(metadata)
           && runCount <= (encoded.size() - kHeaderSize) / (kRunHeaderSize + 1U);
}

[[nodiscard]] inline bool append_runs(std::span<const std::byte> source,
                                      std::span<std::byte> encoded,
                                      std::size_t& cursor,
                                      std::uint16_t& runCount) noexcept {
    std::size_t offset = 0;
    while (offset < source.size()) {
        while (offset < source.size() && source[offset] == std::byte{}) {
            ++offset;
        }
        if (offset == source.size()) {
            break;
        }
        const std::size_t begin = offset;
        while (offset < source.size() && source[offset] != std::byte{}) {
            ++offset;
        }
        const std::size_t length = offset - begin;
        if (begin > 0xFFFFU || length == 0U || length > 0xFFFFU
            || cursor > encoded.size()
            || kRunHeaderSize + length > encoded.size() - cursor
            || runCount == 0xFFFFU) {
            return false;
        }
        if (!write_at(encoded, cursor, static_cast<std::uint16_t>(begin))
            || !write_at(encoded, cursor + 2U, static_cast<std::uint16_t>(length))) {
            return false;
        }
        cursor += kRunHeaderSize;
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(begin),
                    length,
                    encoded.begin() + static_cast<std::ptrdiff_t>(cursor));
        cursor += length;
        ++runCount;
    }
    return runCount != 0U;
}

[[nodiscard]] inline bool compress(std::span<const std::byte> decoded,
                                   std::span<const std::byte> state,
                                   std::span<std::byte> encoded,
                                   std::size_t& written,
                                   Metadata* metadataOutput = nullptr) noexcept {
    written = 0;
    if (decoded.size() != kDecodedSize || state.size() != kStateSize
        || encoded.size() < kHeaderSize) {
        return false;
    }
    Metadata metadata{};
    metadata.revision = read_at<std::uint32_t>(decoded, kRevisionOffset);
    metadata.baseRevision = read_at<std::int32_t>(decoded, kBaseRevisionOffset);
    metadata.expectedHash = read_at<std::uint32_t>(decoded, kHashOffset);
    metadata.memberCount = read_at<std::uint16_t>(decoded, kMemberCountOffset);
    metadata.playerCount = read_at<std::uint16_t>(decoded, kPlayerCountOffset);
    metadata.stateHash = metadata.expectedHash;
    if (read_at<std::uint32_t>(state, kStateRevisionOffset) != metadata.revision
        || read_at<std::uint32_t>(state, kStateMemberCountOffset) != metadata.memberCount
        || read_at<std::uint32_t>(state, kStatePlayerCountOffset) != metadata.playerCount) {
        return false;
    }

    std::fill(encoded.begin(), encoded.end(), std::byte{});
    std::size_t cursor = kHeaderSize;
    if (!append_runs(decoded, encoded, cursor, metadata.decodedRunCount)
        || !append_runs(state, encoded, cursor, metadata.stateRunCount)
        || !valid_metadata(metadata)
        || !write_at(encoded, 0U, kMagic)
        || !write_at(encoded, 4U, kVersion)
        || !write_at(encoded, 6U, static_cast<std::uint16_t>(kHeaderSize))
        || !write_at(encoded, 8U, static_cast<std::uint32_t>(kDecodedSize))
        || !write_at(encoded, 12U, metadata.revision)
        || !write_at(encoded, 16U, metadata.baseRevision)
        || !write_at(encoded, 20U, metadata.expectedHash)
        || !write_at(encoded, 24U, metadata.memberCount)
        || !write_at(encoded, 26U, metadata.playerCount)
        || !write_at(encoded, 28U, metadata.decodedRunCount)
        || !write_at(encoded, 30U, static_cast<std::uint16_t>(0U))
        || !write_at(encoded, 32U, static_cast<std::uint32_t>(kStateSize))
        || !write_at(encoded, 36U, metadata.stateHash)
        || !write_at(encoded, 40U, metadata.stateRunCount)
        || !write_at(encoded, 42U, static_cast<std::uint16_t>(0U))
        || !write_at(encoded, 44U, static_cast<std::uint32_t>(0U))) {
        return false;
    }
    written = cursor;
    if (metadataOutput != nullptr) {
        *metadataOutput = metadata;
    }
    return true;
}

[[nodiscard]] inline bool decode_runs(std::span<const std::byte> encoded,
                                      std::size_t& cursor,
                                      std::uint16_t runCount,
                                      std::span<std::byte> destination) noexcept {
    std::fill(destination.begin(), destination.end(), std::byte{});
    std::size_t previousEnd = 0;
    for (std::uint16_t run = 0; run < runCount; ++run) {
        if (cursor > encoded.size() || kRunHeaderSize > encoded.size() - cursor) {
            return false;
        }
        const std::uint16_t offset = read_at<std::uint16_t>(encoded, cursor);
        const std::uint16_t length = read_at<std::uint16_t>(encoded, cursor + 2U);
        cursor += kRunHeaderSize;
        const std::size_t end = static_cast<std::size_t>(offset) + length;
        if (length == 0U || offset < previousEnd || end > destination.size()
            || cursor > encoded.size() || length > encoded.size() - cursor) {
            return false;
        }
        const auto bytes = encoded.subspan(cursor, length);
        if (std::any_of(bytes.begin(), bytes.end(),
                        [](std::byte value) noexcept { return value == std::byte{}; })) {
            return false;
        }
        std::copy(bytes.begin(), bytes.end(),
                  destination.begin() + static_cast<std::ptrdiff_t>(offset));
        cursor += length;
        previousEnd = end;
    }
    return true;
}

[[nodiscard]] inline bool decompress(std::span<const std::byte> encoded,
                                     std::span<std::byte> decoded,
                                     std::span<std::byte> state,
                                     Metadata* metadataOutput = nullptr) noexcept {
    Metadata metadata{};
    if (decoded.size() != kDecodedSize || state.size() != kStateSize
        || !inspect(encoded, metadata)) {
        return false;
    }
    std::size_t cursor = kHeaderSize;
    if (!decode_runs(encoded, cursor, metadata.decodedRunCount, decoded)
        || !decode_runs(encoded, cursor, metadata.stateRunCount, state)
        || cursor != encoded.size()
        || read_at<std::uint32_t>(decoded, kRevisionOffset) != metadata.revision
        || read_at<std::int32_t>(decoded, kBaseRevisionOffset) != metadata.baseRevision
        || read_at<std::uint32_t>(decoded, kHashOffset) != metadata.expectedHash
        || read_at<std::uint16_t>(decoded, kMemberCountOffset) != metadata.memberCount
        || read_at<std::uint16_t>(decoded, kPlayerCountOffset) != metadata.playerCount
        || read_at<std::uint32_t>(state, kStateRevisionOffset) != metadata.revision
        || read_at<std::uint32_t>(state, kStateMemberCountOffset) != metadata.memberCount
        || read_at<std::uint32_t>(state, kStatePlayerCountOffset) != metadata.playerCount) {
        return false;
    }
    if (metadataOutput != nullptr) {
        *metadataOutput = metadata;
    }
    return true;
}

// the joiner's snapshot is base revision zero, and replaying it into the owner's established
// PRIMARY resets its native tables; so apply only the remote peer and player delta instead
[[nodiscard]] inline bool rebase_solo_owner(
    std::span<std::byte> decoded,
    std::span<std::byte> projectedState,
    std::span<const std::byte> currentState,
    std::uint64_t expectedOwnerPlatformId,
    OwnerRebaseSummary* summaryOutput = nullptr,
    std::uint64_t expectedRemotePlatformId = 0U,
    const char** failureOutput = nullptr) noexcept {
    if (summaryOutput != nullptr) {
        *summaryOutput = {};
    }
    if (failureOutput != nullptr) {
        *failureOutput = "none";
    }
    const auto reject = [failureOutput](const char* reason) noexcept {
        if (failureOutput != nullptr) {
            *failureOutput = reason;
        }
        return false;
    };
    if (decoded.size() != kDecodedSize || projectedState.size() != kStateSize
        || currentState.size() != kStateSize || expectedOwnerPlatformId == 0U) {
        return reject("input_shape");
    }

    const std::span<const std::byte> decodedView(decoded.data(), decoded.size());
    const std::span<const std::byte> projectedView(projectedState.data(),
                                                   projectedState.size());
    const auto equal_bytes = [](std::span<const std::byte> left,
                                std::size_t leftOffset,
                                std::span<const std::byte> right,
                                std::size_t rightOffset,
                                std::size_t length) noexcept {
        return leftOffset <= left.size() && length <= left.size() - leftOffset
               && rightOffset <= right.size() && length <= right.size() - rightOffset
               && std::memcmp(left.data() + leftOffset,
                              right.data() + rightOffset,
                              length)
                      == 0;
    };
    constexpr std::array<std::size_t, 7U> kRequiredMemberPresenceOffsets{
        0x00U, 0x68U, 0x80U, 0x90U, 0xA0U, 0xB4U, 0xB6U};
    const auto member_presence_byte = [&](std::size_t offset) noexcept {
        return std::find(kRequiredMemberPresenceOffsets.begin(),
                         kRequiredMemberPresenceOffsets.end(),
                         offset)
               != kRequiredMemberPresenceOffsets.end();
    };
    const std::uint32_t currentMemberCount =
        read_at<std::uint32_t>(currentState, kStateMemberCountOffset);
    const std::uint32_t currentPlayerCount =
        read_at<std::uint32_t>(currentState, kStatePlayerCountOffset);
    const std::uint32_t projectedMemberCount =
        read_at<std::uint32_t>(projectedView, kStateMemberCountOffset);
    const std::uint32_t projectedPlayerCount =
        read_at<std::uint32_t>(projectedView, kStatePlayerCountOffset);
    const std::uint32_t projectedPlayerAdditionNumber =
        read_at<std::uint32_t>(projectedView, kStatePlayerAdditionNumberOffset);
    if (currentMemberCount != 1U || currentPlayerCount != 1U
        || projectedMemberCount != 2U || projectedPlayerCount != projectedMemberCount
        || read_at<std::uint16_t>(decodedView, kMemberCountOffset)
               != projectedMemberCount
        || read_at<std::uint16_t>(decodedView, kPlayerCountOffset)
               != projectedPlayerCount
        || read_at<std::int32_t>(decodedView, kBaseRevisionOffset) != 0
        || read_at<std::uint32_t>(currentState, kStateHostIndexOffset) != 0U
        || read_at<std::uint32_t>(currentState, kStateSuccessionIndexOffset) != 0U
        || read_at<std::uint64_t>(currentState, kStateMemberMaskOffset) != 1U
        || read_at<std::uint64_t>(currentState, kStateMemberArrayCountOffset) != 1U
        || read_at<std::uint32_t>(projectedView, kStateHostIndexOffset) != 0U
        || read_at<std::uint32_t>(projectedView, kStateSuccessionIndexOffset) != 0U
        || projectedPlayerAdditionNumber > kPlayerAdditionNumberMask
        || read_at<std::uint8_t>(decodedView, kPlayerAdditionNumberPresentOffset) != 1U
        || read_at<std::uint32_t>(decodedView, kPlayerAdditionNumberOffset)
               != projectedPlayerAdditionNumber) {
        return reject("owner_and_projection_shape");
    }

    const std::uint64_t projectedMemberMask =
        (std::uint64_t{1} << projectedMemberCount) - 1U;
    const std::uint32_t projectedPlayerMask =
        (std::uint32_t{1} << projectedPlayerCount) - 1U;
    if (read_at<std::uint64_t>(projectedView, kStateMemberMaskOffset)
            != projectedMemberMask
        || read_at<std::uint64_t>(projectedView, kStateMemberArrayCountOffset)
               != projectedMemberCount
        || read_at<std::uint32_t>(projectedView, kStatePlayerMaskOffset)
               != projectedPlayerMask) {
        return reject("projection_masks");
    }

    const std::size_t currentMemberBase = kStateMemberArrayOffset;
    const std::uint32_t ownerPlayerSlot = read_at<std::uint32_t>(
        currentState, currentMemberBase + kMemberPlayerSlotOffset);
    if (read_at<std::uint64_t>(currentState,
                               currentMemberBase + kMemberAddressLengthOffset)
            != kNativeMemberAddressLength
        || read_at<std::uint64_t>(currentState,
                                  currentMemberBase + kMemberMachineLengthOffset)
               != kNativeMemberMachineLength
        || read_at<std::uint32_t>(currentState,
                                  currentMemberBase + kMemberOwnedPlayerCountOffset)
               != 1U
        || read_at<std::uint64_t>(currentState,
                                  currentMemberBase + kMemberPlayerSlotCountOffset)
               != 1U
        || ownerPlayerSlot >= kNativePlayerCapacity
        || read_at<std::uint32_t>(currentState, kStatePlayerMaskOffset)
               != (std::uint32_t{1} << ownerPlayerSlot)) {
        return reject("owner_member_cardinality");
    }

    const std::size_t currentPlayerBase =
        kStatePlayerArrayOffset + ownerPlayerSlot * kStatePlayerStride;
    const std::uint64_t ownerMachineId = read_at<std::uint64_t>(
        currentState, currentMemberBase + kMemberMachineOffset);
    const std::uint64_t ownerJoinId = read_at<std::uint64_t>(
        currentState, currentMemberBase + kMemberJoinIdOffset);
    const std::uint64_t ownerPlayerId = read_at<std::uint64_t>(
        currentState, currentPlayerBase + kStatePlayerIdOffset);
    const std::uint32_t ownerPlayerSequence = read_at<std::uint32_t>(
        currentState, currentPlayerBase + kStatePlayerSequenceOffset);
    const std::uint64_t ownerNativePlatformId = read_at<std::uint64_t>(
        currentState, currentPlayerBase + kStatePlayerIdentityOffset);
    if (ownerMachineId == 0U) {
        return reject("owner_machine_zero");
    }
    // retail clears the temporary join token once the local host is _established, so a zero
    // owner join id is canonical; member zero is preserved and never re-added by this delta
    if (ownerPlayerId == 0U) {
        return reject("owner_player_zero");
    }
    if (read_at<std::uint32_t>(currentState,
                               currentPlayerBase + kStatePlayerMemberOffset)
        != 0U) {
        return reject("owner_player_member");
    }
    if (read_at<std::uint32_t>(currentState,
                               currentPlayerBase + kStatePlayerOwnedIndexOffset)
        != 0U) {
        return reject("owner_player_owned_index");
    }
    if (ownerNativePlatformId != 0U
        && ownerNativePlatformId != expectedOwnerPlatformId) {
        return reject("owner_native_platform");
    }

    std::array<std::uint64_t, kNativeMemberCapacity> machines{};
    std::array<std::uint64_t, kNativeMemberCapacity> joins{};
    std::array<std::uint64_t, kNativeMemberCapacity> playerIds{};
    std::array<std::uint64_t, kNativeMemberCapacity> platformIds{};
    std::array<std::uint32_t, kNativeMemberCapacity> playerSlots{};
    std::array<std::uint32_t, kNativeMemberCapacity> playerSequences{};
    std::array<std::uint32_t, kNativeMemberCapacity> peerStates{};
    std::array<std::size_t, kNativeMemberCapacity> decodedPeerOrdinals{};
    std::array<std::size_t, kNativePlayerCapacity> decodedPlayerOrdinals{};
    decodedPeerOrdinals.fill(kNativeMemberCapacity);
    decodedPlayerOrdinals.fill(kNativePlayerCapacity);

    for (std::size_t member = 0U; member < projectedMemberCount; ++member) {
        const std::size_t stateMember =
            kStateMemberArrayOffset + member * kStateMemberStride;
        const std::size_t decodedMember =
            kDecodedMemberArrayOffset + member * kDecodedMemberStride;
        bool exactMemberMapping = true;
        for (std::size_t offset = 0U; offset < kStateMemberStride; ++offset) {
            const bool presence = member_presence_byte(offset);
            exactMemberMapping = exactMemberMapping
                                 && (presence
                                         ? decodedView[decodedMember + offset] == std::byte{1}
                                               && projectedView[stateMember + offset]
                                                      == std::byte{}
                                         : decodedView[decodedMember + offset]
                                               == projectedView[stateMember + offset]);
        }
        if (read_at<std::uint64_t>(projectedView,
                                   stateMember + kMemberAddressLengthOffset)
                != kNativeMemberAddressLength
            || read_at<std::uint64_t>(projectedView,
                                      stateMember + kMemberMachineLengthOffset)
                   != kNativeMemberMachineLength
            || read_at<std::uint32_t>(projectedView,
                                      stateMember + kMemberOwnedPlayerCountOffset)
                   != 1U
            || read_at<std::uint64_t>(projectedView,
                                      stateMember + kMemberPlayerSlotCountOffset)
                   != 1U
            || !exactMemberMapping) {
            return reject("projection_member_mapping");
        }

        machines[member] = read_at<std::uint64_t>(
            projectedView, stateMember + kMemberMachineOffset);
        joins[member] = read_at<std::uint64_t>(
            projectedView, stateMember + kMemberJoinIdOffset);
        playerSlots[member] = read_at<std::uint32_t>(
            projectedView, stateMember + kMemberPlayerSlotOffset);
        if (machines[member] == 0U || joins[member] == 0U
            || playerSlots[member] >= kNativePlayerCapacity
            || (projectedPlayerMask & (std::uint32_t{1} << playerSlots[member])) == 0U) {
            return reject("projection_member_identity");
        }
        for (std::size_t prior = 0U; prior < member; ++prior) {
            if (machines[prior] == machines[member]
                || playerSlots[prior] == playerSlots[member]
                || (member != 0U && prior != 0U && joins[prior] == joins[member])) {
                return reject("projection_member_unique");
            }
        }
    }
    if (playerSlots[0] != ownerPlayerSlot) {
        return reject("owner_player_slot");
    }

    for (std::size_t ordinal = 0U; ordinal < projectedMemberCount; ++ordinal) {
        const std::size_t decodedPeer =
            kDecodedPeerArrayOffset + ordinal * kDecodedPeerStride;
        const std::uint32_t member = read_at<std::uint32_t>(
            decodedView, decodedPeer + kDecodedPeerIndexOffset);
        if (member >= projectedMemberCount
            || decodedPeerOrdinals[member] != kNativeMemberCapacity
            || read_at<std::uint32_t>(decodedView,
                                      decodedPeer + kDecodedPeerFullOffset)
                   != 1U
            || read_at<std::uint8_t>(decodedView,
                                     decodedPeer + kDecodedPeerConnectionPresentOffset)
                   > 1U) {
            return reject("projection_peer_shape");
        }
        decodedPeerOrdinals[member] = ordinal;
        const std::size_t statePeer =
            kStatePeerArrayOffset + member * kStatePeerStride;
        peerStates[member] = read_at<std::uint32_t>(
            projectedView, statePeer + kStatePeerStateOffset);
        if (peerStates[member] > 0xFU
            || read_at<std::uint32_t>(decodedView,
                                      decodedPeer + kDecodedPeerStateOffset)
                   != peerStates[member]) {
            return reject("projection_peer_state");
        }
    }

    for (std::size_t ordinal = 0U; ordinal < projectedPlayerCount; ++ordinal) {
        const std::size_t decodedPlayer =
            kDecodedPlayerArrayOffset + ordinal * kDecodedPlayerStride;
        const std::uint32_t slot = read_at<std::uint32_t>(
            decodedView, decodedPlayer + kDecodedPlayerIndexOffset);
        if (slot >= kNativePlayerCapacity
            || decodedPlayerOrdinals[slot] != kNativePlayerCapacity
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerFullOffset)
                   != 1U
            || read_at<std::uint8_t>(decodedView,
                                     decodedPlayer + kDecodedPlayerPresentOffset)
                   != 1U
            || read_at<std::uint8_t>(decodedView,
                                     decodedPlayer + kDecodedPlayerProfilePresentOffset)
                   != 1U) {
            return reject("projection_player_shape");
        }
        decodedPlayerOrdinals[slot] = ordinal;
        const std::size_t statePlayer =
            kStatePlayerArrayOffset + slot * kStatePlayerStride;
        const std::uint32_t member = read_at<std::uint32_t>(
            projectedView, statePlayer + kStatePlayerMemberOffset);
        const std::uint32_t ownedIndex = read_at<std::uint32_t>(
            projectedView, statePlayer + kStatePlayerOwnedIndexOffset);
        if (member >= projectedMemberCount || playerSlots[member] != slot
            || ownedIndex != 0U
            || read_at<std::uint64_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerIdOffset)
                   != read_at<std::uint64_t>(projectedView,
                                             statePlayer + kStatePlayerIdOffset)
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerMemberOffset)
                   != member
            || read_at<std::uint8_t>(decodedView,
                                     decodedPlayer + kDecodedPlayerOwnedIndexOffset)
                   != ownedIndex
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerOwnedIndexOffset)
                   != 0U
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerSequenceOffset)
                   != read_at<std::uint32_t>(projectedView,
                                             statePlayer + kStatePlayerSequenceOffset)
            || read_at<std::uint8_t>(decodedView,
                                     decodedPlayer + kDecodedPlayerFlagOffset)
                   != read_at<std::uint8_t>(projectedView,
                                            statePlayer + kStatePlayerFlagOffset)
            || read_at<std::uint32_t>(projectedView,
                                      statePlayer + kStatePlayerFlagOffset)
                   > 0xFFU
            || read_at<std::uint16_t>(
                   decodedView,
                   decodedPlayer + kDecodedPlayerProfilePresentOffset + 1U)
                   != 0U
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerEmblemOffset)
                   != read_at<std::uint32_t>(projectedView,
                                             statePlayer + kStatePlayerEmblemOffset)
            || read_at<std::uint32_t>(decodedView,
                                      decodedPlayer + kDecodedPlayerProfileTypeOffset)
                   != read_at<std::uint32_t>(projectedView,
                                             statePlayer + kStatePlayerProfileTypeOffset)
            || read_at<std::uint32_t>(projectedView,
                                      statePlayer + kStatePlayerProfileTypeOffset)
                   == kBarePlayerProfileSentinel
            || !equal_bytes(decodedView,
                            decodedPlayer + kDecodedPlayerProfilePayloadOffset,
                            projectedView,
                            statePlayer + kStatePlayerProfilePayloadOffset,
                            kStatePlayerProfilePayloadSize)) {
            return reject("projection_player_mapping");
        }

        playerIds[member] = read_at<std::uint64_t>(
            projectedView, statePlayer + kStatePlayerIdOffset);
        platformIds[member] = read_at<std::uint64_t>(
            projectedView, statePlayer + kStatePlayerIdentityOffset);
        playerSequences[member] = read_at<std::uint32_t>(
            projectedView, statePlayer + kStatePlayerSequenceOffset);
        if (playerIds[member] == 0U || platformIds[member] == 0U) {
            return reject("projection_player_identity");
        }
    }

    if (platformIds[0] != expectedOwnerPlatformId) {
        return reject("projection_owner_platform");
    }
    bool expectedRemotePresent = expectedRemotePlatformId == 0U;
    for (std::size_t member = 1U; member < projectedMemberCount; ++member) {
        expectedRemotePresent = expectedRemotePresent
                                || platformIds[member] == expectedRemotePlatformId;
        if (machines[member] == ownerMachineId
            || (ownerJoinId != 0U && joins[member] == ownerJoinId)
            || playerIds[member] == ownerPlayerId
            || playerSequences[member] == ownerPlayerSequence
            || platformIds[member] == expectedOwnerPlatformId) {
            return reject("projection_remote_collision");
        }
        for (std::size_t prior = 1U; prior < member; ++prior) {
            if (playerIds[prior] == playerIds[member]
                || playerSequences[prior] == playerSequences[member]
                || platformIds[prior] == platformIds[member]) {
                return reject("projection_remote_duplicate");
            }
        }
    }
    if (!expectedRemotePresent) {
        return reject("projection_remote_platform");
    }

    const std::uint32_t currentRevision =
        read_at<std::uint32_t>(currentState, kStateRevisionOffset);
    const std::uint32_t currentPlayerAdditionNumber =
        read_at<std::uint32_t>(currentState, kStatePlayerAdditionNumberOffset);
    const std::size_t remoteMember = 1U;
    const std::size_t remotePlayerSlot = playerSlots[remoteMember];
    const std::size_t remotePeerOrdinal = decodedPeerOrdinals[remoteMember];
    const std::size_t remotePlayerOrdinal = decodedPlayerOrdinals[remotePlayerSlot];
    if (currentRevision == 0U
        || currentRevision >= (std::numeric_limits<std::uint32_t>::max)()
        || currentPlayerAdditionNumber > kPlayerAdditionNumberMask
        || remotePeerOrdinal >= projectedMemberCount
        || remotePlayerOrdinal >= projectedPlayerCount) {
        return reject("owner_revision_or_remote_ordinal");
    }

    std::array<std::byte, kStateMemberStride> remoteMemberState{};
    std::array<std::byte, kStatePlayerStride> remotePlayerState{};
    std::array<std::byte, kDecodedPeerStride> remotePeerDelta{};
    std::array<std::byte, kDecodedPlayerStride> remotePlayerDelta{};
    std::copy_n(projectedState.begin()
                    + static_cast<std::ptrdiff_t>(
                        kStateMemberArrayOffset + remoteMember * kStateMemberStride),
                remoteMemberState.size(),
                remoteMemberState.begin());
    std::copy_n(projectedState.begin()
                    + static_cast<std::ptrdiff_t>(
                        kStatePlayerArrayOffset + remotePlayerSlot * kStatePlayerStride),
                remotePlayerState.size(),
                remotePlayerState.begin());
    std::copy_n(decoded.begin()
                    + static_cast<std::ptrdiff_t>(
                        kDecodedPeerArrayOffset + remotePeerOrdinal * kDecodedPeerStride),
                remotePeerDelta.size(),
                remotePeerDelta.begin());
    std::copy_n(decoded.begin()
                    + static_cast<std::ptrdiff_t>(
                        kDecodedPlayerArrayOffset
                        + remotePlayerOrdinal * kDecodedPlayerStride),
                remotePlayerDelta.size(),
                remotePlayerDelta.begin());

    // a new add in the owner's existing session, not a replay of the joiner's history: allocate
    // from the owner's counter (PC 1.59 normalizes it to twenty bits after each increment)
    const std::uint32_t rebasedRemotePlayerSequence = currentPlayerAdditionNumber;
    const std::uint32_t nextPlayerAdditionNumber =
        (currentPlayerAdditionNumber + 1U) & kPlayerAdditionNumberMask;
    if (!write_at(remotePlayerState,
                  kStatePlayerSequenceOffset,
                  rebasedRemotePlayerSequence)
        || !write_at(remotePlayerDelta,
                     kDecodedPlayerSequenceOffset,
                     rebasedRemotePlayerSequence)) {
        return reject("remote_addition_number");
    }

    const bool remoteConnectionWasPresent =
        read_at<std::uint8_t>(remotePeerDelta, kDecodedPeerConnectionPresentOffset) != 0U;

    // predict a true incremental apply: native-only bytes and the owner's peer/player
    // incarnations stay byte-exact because a nonzero-base delta doesn't reset them
    std::copy(currentState.begin(), currentState.end(), projectedState.begin());
    if (!write_at(projectedState, kStateHostIndexOffset, std::uint32_t{0U})
        || !write_at(projectedState, kStateSuccessionIndexOffset, std::uint32_t{0U})
        || !write_at(projectedState,
                     kStateMemberCountOffset,
                     static_cast<std::uint32_t>(projectedMemberCount))
        || !write_at(projectedState, kStateMemberMaskOffset, projectedMemberMask)
        || !write_at(projectedState,
                     kStateMemberArrayCountOffset,
                     static_cast<std::uint64_t>(projectedMemberCount))
        || !write_at(projectedState,
                     kStatePlayerCountOffset,
                     static_cast<std::uint32_t>(projectedPlayerCount))
        || !write_at(projectedState,
                     kStatePlayerMaskOffset,
                     (std::uint32_t{1} << ownerPlayerSlot)
                         | (std::uint32_t{1} << remotePlayerSlot))
        || !write_at(projectedState,
                     kStatePlayerAdditionNumberOffset,
                     nextPlayerAdditionNumber)) {
        return reject("predicted_state_header");
    }
    std::copy(remoteMemberState.begin(),
              remoteMemberState.end(),
              projectedState.begin()
                  + static_cast<std::ptrdiff_t>(
                      kStateMemberArrayOffset + remoteMember * kStateMemberStride));
    std::copy(remotePlayerState.begin(),
              remotePlayerState.end(),
              projectedState.begin()
                  + static_cast<std::ptrdiff_t>(
                      kStatePlayerArrayOffset + remotePlayerSlot * kStatePlayerStride));
    const std::size_t projectedRemotePeer =
        kStatePeerArrayOffset + remoteMember * kStatePeerStride;
    std::fill_n(projectedState.begin()
                    + static_cast<std::ptrdiff_t>(projectedRemotePeer),
                kStatePeerStride,
                std::byte{});
    if (!write_at(projectedState,
                  projectedRemotePeer + kStatePeerStateOffset,
                  peerStates[remoteMember])) {
        return reject("predicted_remote_peer");
    }

    // rebuild the owner row from its exact canonical row, then restore the seven field-presence
    // markers; copying only known spans left joiner-side bytes and failed valid projections
    std::copy_n(currentState.begin()
                    + static_cast<std::ptrdiff_t>(kStateMemberArrayOffset),
                kStateMemberStride,
                decoded.begin()
                    + static_cast<std::ptrdiff_t>(kDecodedMemberArrayOffset));
    for (const std::size_t offset : kRequiredMemberPresenceOffsets) {
        decoded[kDecodedMemberArrayOffset + offset] = std::byte{1};
    }

    // omitting owner deltas keeps the owner's host incarnation and connection state untouched
    std::fill_n(decoded.begin()
                    + static_cast<std::ptrdiff_t>(kDecodedPeerArrayOffset),
                kNativeStateMemberCapacity * kDecodedPeerStride,
                std::byte{});
    std::copy(remotePeerDelta.begin(),
              remotePeerDelta.end(),
              decoded.begin() + static_cast<std::ptrdiff_t>(kDecodedPeerArrayOffset));
    std::fill_n(decoded.begin()
                    + static_cast<std::ptrdiff_t>(
                        kDecodedPeerArrayOffset + kDecodedPeerConnectionPresentOffset),
                kDecodedPeerStride - kDecodedPeerConnectionPresentOffset,
                std::byte{});
    if (!write_at(decoded,
                  kDecodedPeerArrayOffset + kDecodedPeerStateOffset,
                  peerStates[remoteMember])) {
        return reject("decoded_delta_header");
    }

    std::fill_n(decoded.begin()
                    + static_cast<std::ptrdiff_t>(kDecodedPlayerArrayOffset),
                kNativePlayerCapacity * kDecodedPlayerStride,
                std::byte{});
    std::copy(remotePlayerDelta.begin(),
              remotePlayerDelta.end(),
              decoded.begin() + static_cast<std::ptrdiff_t>(kDecodedPlayerArrayOffset));
    if (!write_at(decoded,
                  kBaseRevisionOffset,
                  static_cast<std::int32_t>(currentRevision))
        || !write_at(decoded, kMemberCountOffset, std::uint16_t{1U})
        || !write_at(decoded, kPlayerCountOffset, std::uint16_t{1U})
        || !write_at(decoded, kPlayerAdditionNumberPresentOffset, std::uint8_t{1U})
        || !write_at(decoded, kPlayerAdditionNumberOffset, nextPlayerAdditionNumber)) {
        return reject("decoded_delta_header");
    }

    for (std::size_t offset = 0U; offset < kStateMemberStride; ++offset) {
        const bool presence = member_presence_byte(offset);
        if ((presence
             && (decoded[kDecodedMemberArrayOffset + offset] != std::byte{1}
                 || projectedState[kStateMemberArrayOffset + offset] != std::byte{}))
            || (!presence
                && decoded[kDecodedMemberArrayOffset + offset]
                       != projectedState[kStateMemberArrayOffset + offset])) {
            return reject("decoded_owner_normalization");
        }
    }

    if (summaryOutput != nullptr) {
        summaryOutput->ownerMachineId = ownerMachineId;
        summaryOutput->ownerJoinId = ownerJoinId;
        summaryOutput->ownerPlayerId = ownerPlayerId;
        summaryOutput->ownerPlayerSlot = ownerPlayerSlot;
        summaryOutput->ownerPlayerSequence = ownerPlayerSequence;
        summaryOutput->remoteMachineId = machines[1];
        summaryOutput->remoteJoinId = joins[1];
        summaryOutput->remotePlayerId = playerIds[1];
        summaryOutput->remotePlatformId = platformIds[1];
        summaryOutput->remotePlayerSequence = rebasedRemotePlayerSequence;
        summaryOutput->priorPlayerAdditionNumber = currentPlayerAdditionNumber;
        summaryOutput->nextPlayerAdditionNumber = nextPlayerAdditionNumber;
        summaryOutput->clearedRemoteConnections = remoteConnectionWasPresent ? 1U : 0U;
    }
    return true;
}

} // namespace dawn::middleware::social::membership_projection
