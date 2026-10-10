#include "session_messages.h"

#include <array>

#include "../../encoding/bit_raw.h"
#include "../../protobuf/codec.h"
#include "session_state.h"

namespace dawn::middleware::gameplay::group {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kProtocolWidth = 16;
constexpr std::uint8_t kSequenceWidth = 32;
constexpr std::uint8_t kFlagWidth = 1;
constexpr std::uint8_t kBootKindWidth = 3;
constexpr std::uint8_t kBootReasonWidth = 5;
constexpr std::uint8_t kNetAddrMethodWidth = 3;
constexpr std::size_t kShortNetAddrBytes = 41;
constexpr std::size_t kLongNetAddrBytes = 85;
constexpr std::uint8_t kFirstLongNetAddrMethod = 6;
constexpr std::size_t kNetAddrMethodOffset = descriptor::kNetAddrSize - 1;
constexpr std::uint8_t kHostHandoffMemberWidth = 6;
constexpr std::uint8_t kHostTransitionCountWidth = 7;
constexpr std::uint32_t kHostTransitionCountMaximum = 100;
constexpr std::uint8_t kSampleWidth = 64;

[[nodiscard]] bool
write_serialized_net_addr(bits::Writer& writer,
                          const std::array<std::byte, descriptor::kNetAddrSize>& address) noexcept {
    const std::uint8_t method = static_cast<std::uint8_t>(address[kNetAddrMethodOffset]);
    if (method >= (std::uint8_t{1} << kNetAddrMethodWidth)) {
        return false;
    }
    const std::size_t addressBytes =
        method >= kFirstLongNetAddrMethod ? kLongNetAddrBytes : kShortNetAddrBytes;
    return writer.write(method, kNetAddrMethodWidth)
           && bits::write_raw(writer, std::span(address).first(addressBytes));
}

[[nodiscard]] bool
read_serialized_net_addr(bits::Reader& reader,
                         std::array<std::byte, descriptor::kNetAddrSize>& address) noexcept {
    std::uint64_t methodValue = 0;
    if (!reader.read(kNetAddrMethodWidth, methodValue)) {
        return false;
    }
    const auto method = static_cast<std::uint8_t>(methodValue);
    const std::size_t addressBytes =
        method >= kFirstLongNetAddrMethod ? kLongNetAddrBytes : kShortNetAddrBytes;
    std::array<std::byte, descriptor::kNetAddrSize> candidate{};
    if (!bits::read_raw(reader, std::span(candidate).first(addressBytes))) {
        return false;
    }
    candidate[kNetAddrMethodOffset] = static_cast<std::byte>(method);
    address = candidate;
    return true;
}

constexpr std::uint32_t kRootRevision = 1;
constexpr std::uint32_t kRootHostIndex = 2;
constexpr std::uint32_t kRootSuccession = 3;
constexpr std::uint32_t kRootMemberCount = 4;
constexpr std::uint32_t kRootMemberMask = 5;
constexpr std::uint32_t kRootMember = 6;

constexpr std::uint32_t kMemberAddress = 1;
constexpr std::uint32_t kMemberMachineId = 2;
constexpr std::uint32_t kMemberJoinId = 3;
constexpr std::uint32_t kMemberIdB = 8;
constexpr std::uint32_t kMemberPlayerCount = 9;
constexpr std::uint32_t kMemberPlayerSlot = 10;
constexpr std::uint32_t kMemberFlagA = 11;
constexpr std::uint32_t kMemberFlagB = 12;

constexpr std::uint64_t kMemberIdEmpty = 0;
constexpr std::uint64_t kMemberOwnedPlayerCount = 1;
constexpr std::uint64_t kMemberFlagClear = 0;

constexpr std::uint8_t kProtobufLengthWidth = 13;
constexpr std::uint8_t kRevisionWidth = 32;
constexpr std::uint8_t kDeltaCountWidth = 6;
constexpr std::uint8_t kDeltaIndexWidth = 6;
constexpr std::uint8_t kMemberStateWidth = 4;
constexpr std::uint8_t kConnectionValueWidth = 8;
constexpr std::uint8_t kWordWidth = 32;
// value field, so it goes out most significant byte first
constexpr std::uint8_t kJoinTimestampWidth = 64;

constexpr std::uint64_t kRevisionPairPresent = 0;
constexpr std::uint32_t kCompleteSnapshotBase = 0;
constexpr std::uint32_t kUnreadWord = 0;
constexpr std::uint64_t kDeltaEntryFull = 1;
constexpr std::uint8_t kPlayerIndexWidth = 5;
constexpr std::uint8_t kPlayerMemberWidth = 6;
// the decoder refuses any value but zero, so a member publishes at most one player this way
constexpr std::uint8_t kPlayerOwnedIndexWidth = 1;
constexpr std::uint8_t kPlayerSequenceWidth = kPlayerAdditionNumberWidth;
constexpr std::uint64_t kPlayerOwnedIndexZero = 0;
constexpr std::uint64_t kPlayerProfileAbsent = 0;
constexpr std::uint64_t kPlayerProfilePresent = 1;
constexpr std::uint8_t kPlayerProfileWordWidth = 32;
constexpr std::uint8_t kPlayerProfileTypeWidth = 3;
constexpr std::int32_t kPlayerProfileTypeMinimum = -1;
constexpr std::int32_t kPlayerProfileTypeMaximum = 6;
constexpr std::uint8_t kPlayerProfileNameUnitWidth = 16;
constexpr std::array<std::uint8_t, 3> kPlayerProfileTailScalarWidths{5, 2, 1};
constexpr std::size_t kPlayerProfileCodecBFieldCount = 4;
constexpr std::uint8_t kPlayerProfileField2Width = 8;
constexpr std::uint8_t kPlayerProfileSixBitWidth = 6;
constexpr std::uint8_t kPlayerProfileField4ComponentWidth = 16;
constexpr std::uint8_t kPlayerProfileField5ComponentWidth = 64;
constexpr std::uint8_t kPlayerProfileField6FloatWidth = 32;
constexpr std::uint8_t kPlayerProfileField6SignedWidth = 16;
constexpr std::uint8_t kPlayerProfileField6ByteWidth = 8;
constexpr std::int32_t kPlayerProfileField6SignedMinimum = -32768;
constexpr std::uint8_t kPlayerProfileTailOptionalBit = 4;
constexpr std::uint64_t kEntryFieldAbsent = 0;
constexpr std::uint64_t kTailGroupAbsent = 0;
constexpr std::uint64_t kTailGroupPresent = 1;
constexpr std::size_t kTailGroupCount = 4;
constexpr std::size_t kProtobufCapacity = 5972;
constexpr std::size_t kMemberBytes = 128;
constexpr std::size_t kMachineIdBytes = 8;
constexpr unsigned kByteBits = 8;
constexpr std::uint64_t kByteMask = 0xFF;

// PC 1.59 writes the canonical UTF-16 units through the terminator; the server mirrors its
// read-side masking separately when computing the client's expected state hash
[[nodiscard]] bool write_player_name(
    bits::Writer& writer,
    const std::array<char16_t, kMembershipDisplayNameCapacity>& displayName) noexcept {
    for (const char16_t character : displayName) {
        const auto unit = static_cast<std::uint16_t>(character);
        if (!writer.write(unit, kPlayerProfileNameUnitWidth)) {
            return false;
        }
        if (unit == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool write_member(protobuf::Writer& writer, const MembershipMember& member) noexcept {
    std::array<std::byte, kMachineIdBytes> machineId{};
    for (std::size_t index = 0; index < machineId.size(); ++index) {
        machineId[index] =
            static_cast<std::byte>((member.machineId >> (index * kByteBits)) & kByteMask);
    }

    std::array<std::byte, kMemberBytes> storage{};
    protobuf::Writer body(storage);
    if (!body.write_length_delimited(kMemberAddress, member.address)
        || !body.write_length_delimited(kMemberMachineId, machineId)
        || !body.write_varint(kMemberJoinId, member.joinId)
        || !body.write_varint(kMemberIdB, kMemberIdEmpty)) {
        return false;
    }
    if (member.ownsPlayerSlot) {
        if (member.ownedPlayerCountPresent
            && !body.write_varint(kMemberPlayerCount, kMemberOwnedPlayerCount)) {
            return false;
        }
        if (!body.write_varint(kMemberPlayerSlot, member.playerSlot)) {
            return false;
        }
    }
    if (!body.write_varint(kMemberFlagA, kMemberFlagClear)
        || !body.write_varint(kMemberFlagB, kMemberFlagClear)) {
        return false;
    }
    return writer.write_length_delimited(kRootMember, {storage.data(), body.size()});
}

[[nodiscard]] bool write_membership_protobuf(const MembershipUpdate& body,
                                             std::span<std::byte> storage,
                                             std::size_t& size) noexcept {
    const std::uint64_t count = static_cast<std::uint64_t>(body.members.size());
    // members occupy indices 0 upward, so the occupied-slot mask follows from the count
    const std::uint64_t mask = (std::uint64_t{1} << count) - 1;
    protobuf::Writer writer(storage);
    if (!writer.write_varint(kRootRevision, body.revision)
        || !writer.write_varint(kRootHostIndex, body.hostMemberIndex)
        || !writer.write_varint(kRootSuccession, body.successionIndex)
        || !writer.write_varint(kRootMemberCount, count)
        || !writer.write_varint(kRootMemberMask, mask)) {
        return false;
    }
    for (const MembershipMember& member : body.members) {
        if (!write_member(writer, member)) {
            return false;
        }
    }
    size = writer.size();
    return true;
}

[[nodiscard]] bool
write_peer_delta(bits::Writer& writer, std::size_t index, const MembershipMember& member) noexcept {
    if (!writer.write(index, kDeltaIndexWidth) || !writer.write(kDeltaEntryFull, kFlagWidth)
        || !writer.write(static_cast<std::uint64_t>(member.state), kMemberStateWidth)
        || !writer.write(member.connectionPresent ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (member.connectionPresent
        && (!writer.write(member.joinCompatibility, kWordWidth)
            || !writer.write(member.joinTimestamp, kJoinTimestampWidth)
            || !writer.write(member.connectionValue, kConnectionValueWidth))) {
        return false;
    }
    return writer.write(kEntryFieldAbsent, kFlagWidth)
           && writer.write(kEntryFieldAbsent, kFlagWidth)
           && writer.write(kEntryFieldAbsent, kFlagWidth);
}

[[nodiscard]] bool write_player_delta(bits::Writer& writer,
                                      const MembershipPlayer& player) noexcept {
    if (!writer.write(player.slot, kPlayerIndexWidth) || !writer.write(kDeltaEntryFull, kFlagWidth)
        || !writer.write(1U, kFlagWidth) || !bits::write_raw_u64(writer, player.playerId)
        || !writer.write(player.memberIndex, kPlayerMemberWidth)
        || !writer.write(kPlayerOwnedIndexZero, kPlayerOwnedIndexWidth)
        || !writer.write(player.addSequence, kPlayerSequenceWidth)
        || !writer.write(player.flag ? 1U : 0U, kFlagWidth)
        || !writer.write(player.profilePresent ? kPlayerProfilePresent : kPlayerProfileAbsent,
                         kFlagWidth)) {
        return false;
    }
    if (!player.profilePresent) {
        return true;
    }
    if (player.profileType < kPlayerProfileTypeMinimum
        || player.profileType > kPlayerProfileTypeMaximum
        || !writer.write(player.emblemDefinitionIndex, kPlayerProfileWordWidth)
        || !writer.write(static_cast<std::uint64_t>(player.profileType + 1),
                         kPlayerProfileTypeWidth)) {
        return false;
    }

    const auto& profile = player.profileSupplement;
    if (!writer.write(player.displayNamePresent ? 1U : 0U, kFlagWidth)
        || (player.displayNamePresent && !write_player_name(writer, player.displayName))
        || !writer.write(player.identityPresent ? 1U : 0U, kFlagWidth)
        || (player.identityPresent && !bits::write_raw(writer, player.identity))) {
        return false;
    }

    if (profile.field2 > 0xFFU
        || !writer.write(profile.field2Present ? 1U : 0U, kFlagWidth)
        || (profile.field2Present
            && !writer.write(profile.field2, kPlayerProfileField2Width))) {
        return false;
    }
    const bool field3Valid = profile.field3 == 0xFFU || profile.field3 < 0x3FU;
    if (!field3Valid || !writer.write(profile.field3Present ? 1U : 0U, kFlagWidth)
        || (profile.field3Present
            && !writer.write(static_cast<std::uint8_t>(profile.field3 + 1U),
                             kPlayerProfileSixBitWidth))) {
        return false;
    }
    const bool field8Valid = profile.field8 == 0xFFU || profile.field8 < 0x3FU;
    if (!field8Valid || !writer.write(profile.field8Present ? 1U : 0U, kFlagWidth)
        || (profile.field8Present
            && !writer.write(static_cast<std::uint8_t>(profile.field8 + 1U),
                             kPlayerProfileSixBitWidth))) {
        return false;
    }
    if (!writer.write(profile.field4Present ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (profile.field4Present) {
        for (const std::int32_t component : profile.field4) {
            if (component < -32768 || component > 32767
                || !writer.write(static_cast<std::uint16_t>(component),
                                 kPlayerProfileField4ComponentWidth)) {
                return false;
            }
        }
    }
    if (!writer.write(profile.field5Present ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (profile.field5Present) {
        for (const std::uint64_t component : profile.field5) {
            if (!writer.write(component, kPlayerProfileField5ComponentWidth)) {
                return false;
            }
        }
    }
    if (!writer.write(profile.field6Present ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (profile.field6Present) {
        if (!writer.write(profile.field6FloatBits, kPlayerProfileField6FloatWidth)) {
            return false;
        }
        for (const std::int16_t component : profile.field6Signed) {
            const auto encoded = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(component) - kPlayerProfileField6SignedMinimum);
            if (!writer.write(encoded, kPlayerProfileField6SignedWidth)) {
                return false;
            }
        }
        if (!writer.write(profile.field6Byte, kPlayerProfileField6ByteWidth)) {
            return false;
        }
    }
    // the durable selected-character light stays authoritative; fields 2 to 6 above are the
    // endpoint's byte-exact association data
    if (!writer.write(1U, kFlagWidth)
        || !writer.write(static_cast<std::uint32_t>(player.light),
                         kPlayerProfileWordWidth)) {
        return false;
    }

    // ID-30 embeds Codec B here. Omitting these four bits makes retail start the next player
    // row four bits late
    for (std::size_t field = 0; field < kPlayerProfileCodecBFieldCount; ++field) {
        if (!writer.write(0U, kFlagWidth)) {
            return false;
        }
    }

    for (const std::uint32_t word : profile.tailWords) {
        if (!writer.write(word, kPlayerProfileWordWidth)) {
            return false;
        }
    }
    if (profile.tailFive >= (1U << kPlayerProfileTailScalarWidths[0])
        || profile.tailTwo >= (1U << kPlayerProfileTailScalarWidths[1])
        || !writer.write(profile.tailFive, kPlayerProfileTailScalarWidths[0])
        || !writer.write(profile.tailTwo, kPlayerProfileTailScalarWidths[1])
        || !writer.write(profile.tailFlag ? 1U : 0U, kPlayerProfileTailScalarWidths[2])) {
        return false;
    }
    if ((profile.tailFive & (1U << kPlayerProfileTailOptionalBit)) != 0) {
        if (profile.tailOptionalFive >= (1U << kPlayerProfileTailScalarWidths[0])
            || !writer.write(profile.tailOptionalPresent ? 1U : 0U, kFlagWidth)
            || (profile.tailOptionalPresent
                && !writer.write(profile.tailOptionalFive,
                                 kPlayerProfileTailScalarWidths[0]))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool write_peer_connect(bits::Writer& writer, const PeerConnect& body) noexcept {
    return writer.write(body.protocolVersion, kProtocolWidth)
           && bits::write_raw_u64(writer, body.machineId)
           && bits::write_raw_u64(writer, body.sessionId);
}

bool write_join_complete(bits::Writer& writer, const JoinComplete& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId)
           && bits::write_raw_u64(writer, body.machineId)
           && writer.write(body.joinSequence, kSequenceWidth);
}

bool read_join_complete(bits::Reader& reader, JoinComplete& output) noexcept {
    JoinComplete candidate{};
    std::uint64_t sequence = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.machineId)
        || !reader.read(kSequenceWidth, sequence)) {
        return false;
    }
    candidate.joinSequence = static_cast<std::uint32_t>(sequence);
    output = candidate;
    return true;
}

bool read_join_abort(bits::Reader& reader, SessionNotice& output) noexcept {
    std::uint64_t flag = 0;
    SessionNotice candidate{};
    if (!bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.machineId) || !reader.read(kFlagWidth, flag)) {
        return false;
    }
    candidate.flag = flag != 0;
    output = candidate;
    return true;
}

bool read_session_only(bits::Reader& reader, std::uint64_t& output) noexcept {
    return bits::read_raw_u64(reader, output);
}

bool write_session_only(bits::Writer& writer, std::uint64_t sessionId) noexcept {
    return bits::write_raw_u64(writer, sessionId);
}

bool write_session_disband(bits::Writer& writer, const SessionNotice& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId)
           && bits::write_raw_u64(writer, body.machineId)
           && writer.write(body.flag ? 1U : 0U, kFlagWidth);
}

bool write_session_boot(bits::Writer& writer, const SessionBoot& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId) && writer.write(body.kind, kBootKindWidth)
           && writer.write(body.reason, kBootReasonWidth)
           && bits::write_raw_u64(writer, body.machineId);
}

bool write_host_handoff(bits::Writer& writer, const HostHandoff& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId)
           && writer.write(body.memberIndex, kHostHandoffMemberWidth)
           && write_serialized_net_addr(writer, body.address);
}

bool write_host_transition(bits::Writer& writer, const HostTransition& body) noexcept {
    const std::uint32_t count =
        body.count > kHostTransitionCountMaximum ? kHostTransitionCountMaximum : body.count;
    const std::span<const std::byte> valueBytes{reinterpret_cast<const std::byte*>(&body.value),
                                                sizeof(body.value)};
    return bits::write_raw_u64(writer, body.sessionId)
           && writer.write(count, kHostTransitionCountWidth) && bits::write_raw(writer, valueBytes);
}

bool write_host_reestablish(bits::Writer& writer, const HostReestablish& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId)
           && bits::write_raw_u64(writer, body.machineId)
           && write_serialized_net_addr(writer, body.address)
           && bits::write_raw(writer, body.identity128)
           && bits::write_raw(writer, body.identity144);
}

bool read_host_reestablish(bits::Reader& reader, HostReestablish& output) noexcept {
    HostReestablish candidate{};
    if (!bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.machineId)
        || !read_serialized_net_addr(reader, candidate.address)
        || !bits::read_raw(reader, candidate.identity128)
        || !bits::read_raw(reader, candidate.identity144)) {
        return false;
    }
    output = candidate;
    return true;
}

bool read_time_synchronize(bits::Reader& reader, TimeSynchronize& output) noexcept {
    std::uint64_t variant = 0;
    TimeSynchronize candidate{};
    if (!bits::read_raw_u64(reader, candidate.sessionId) || !reader.read(kFlagWidth, variant)
        || !reader.read(kSampleWidth, candidate.sampleA)) {
        return false;
    }
    candidate.threeSample = variant != 0;
    if (candidate.threeSample
        && (!reader.read(kSampleWidth, candidate.sampleB)
            || !reader.read(kSampleWidth, candidate.sampleC))) {
        return false;
    }
    output = candidate;
    return true;
}

bool write_time_synchronize(bits::Writer& writer, const TimeSynchronize& body) noexcept {
    if (!bits::write_raw_u64(writer, body.sessionId)
        || !writer.write(body.threeSample ? 1U : 0U, kFlagWidth)
        || !writer.write(body.sampleA, kSampleWidth)) {
        return false;
    }
    if (!body.threeSample) {
        return true;
    }
    return writer.write(body.sampleB, kSampleWidth) && writer.write(body.sampleC, kSampleWidth);
}

bool write_membership_update(bits::Writer& writer, const MembershipUpdate& body) noexcept {
    // the consumer refuses the message unless base revision is below the message revision;
    // a complete snapshot always publishes base revision 0
    if (body.revision == 0 || body.members.size() > kMemberCapacity
        || body.players.size() > kPlayerCapacity
        || body.nextPlayerAdditionNumber > kPlayerAdditionNumberMask) {
        return false;
    }
    for (const MembershipPlayer& player : body.players) {
        if (player.slot >= kPlayerCapacity || player.memberIndex >= kMemberCapacity
            || player.addSequence > kPlayerAdditionNumberMask) {
            return false;
        }
    }
    std::array<std::byte, kProtobufCapacity> protobufStorage{};
    std::size_t protobufSize = 0;
    if (!write_membership_protobuf(body, protobufStorage, protobufSize)) {
        return false;
    }
    if (!bits::write_raw_u64(writer, body.hostMachineId)
        || !writer.write(protobufSize, kProtobufLengthWidth)
        || !bits::write_raw(writer, {protobufStorage.data(), protobufSize})
        || !writer.write(kRevisionPairPresent, kFlagWidth)
        || !writer.write(kCompleteSnapshotBase, kRevisionWidth)
        || !writer.write(kUnreadWord, kRevisionWidth)
        || !writer.write(body.members.size(), kDeltaCountWidth)
        || !writer.write(body.players.size(), kDeltaCountWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < body.members.size(); ++index) {
        if (!write_peer_delta(writer, index, body.members[index])) {
            return false;
        }
    }
    for (const MembershipPlayer& player : body.players) {
        if (!write_player_delta(writer, player)) {
            return false;
        }
    }
    // the archived public Citizen variant omits this group and hashes a cleared generation slot;
    // keep that distinction before its machine map is accepted
    std::size_t firstAbsentGroup = 0;
    if (body.playerAdditionNumberPresent) {
        if (!writer.write(kTailGroupPresent, kFlagWidth)
            || !writer.write(body.nextPlayerAdditionNumber, kPlayerSequenceWidth)) {
            return false;
        }
        firstAbsentGroup = 1;
    }
    for (std::size_t group = firstAbsentGroup; group < kTailGroupCount; ++group) {
        if (!writer.write(kTailGroupAbsent, kFlagWidth)) {
            return false;
        }
    }
    // the consumer hashes its state after applying and compares; replica layout must match
    return writer.write(session_state_hash(body), kWordWidth);
}

} // namespace dawn::middleware::gameplay::group
