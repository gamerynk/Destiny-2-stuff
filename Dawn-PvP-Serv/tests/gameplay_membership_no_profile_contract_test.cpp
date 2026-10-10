#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "middleware/encoding/bit_raw.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/gameplay/group/session_state.h"
#include "middleware/protobuf/codec.h"

namespace {

namespace group = dawn::middleware::gameplay::group;

constexpr std::size_t kPlayerCountOffset = 0x3B50;
constexpr std::size_t kPlayerMaskOffset = 0x3B54;
constexpr std::size_t kPlayerTableOffset = 0x3B58;
constexpr std::size_t kPlayerStride = 0x1A8;
constexpr std::size_t kPlayerAdditionNumberOffset = 0x7058;
constexpr std::size_t kMemberTableOffset = 0x38;
constexpr std::size_t kMemberStride = 0xB8;

[[nodiscard]] bool
protobuf_members_publish_player_counts(std::span<const std::byte> payload,
                                       std::span<const group::MembershipMember> members) noexcept {
    dawn::middleware::protobuf::Reader root(payload);
    dawn::middleware::protobuf::Field field{};
    std::size_t memberIndex = 0;
    while (root.next(field)) {
        if (field.fieldNumber != 6) {
            continue;
        }
        if (field.wireType != dawn::middleware::protobuf::WireType::lengthDelimited
            || memberIndex >= members.size()) {
            return false;
        }
        dawn::middleware::protobuf::Reader member(field.bytes);
        dawn::middleware::protobuf::Field memberField{};
        bool foundPlayerCount = false;
        bool foundPlayerSlot = false;
        while (member.next(memberField)) {
            if (memberField.fieldNumber == 9) {
                foundPlayerCount =
                    memberField.wireType == dawn::middleware::protobuf::WireType::varint
                    && memberField.value == 1;
            } else if (memberField.fieldNumber == 10) {
                foundPlayerSlot =
                    memberField.wireType == dawn::middleware::protobuf::WireType::varint
                    && memberField.value == members[memberIndex].playerSlot;
            }
        }
        const bool expectsPlayerCount = members[memberIndex].ownsPlayerSlot
                                        && members[memberIndex].ownedPlayerCountPresent;
        if (member.remaining() != 0 || foundPlayerCount != expectsPlayerCount
            || foundPlayerSlot != members[memberIndex].ownsPlayerSlot) {
            std::fprintf(stderr, "member %zu player-count/slot protobuf mismatch\n", memberIndex);
            return false;
        }
        ++memberIndex;
    }
    return root.remaining() == 0 && memberIndex == members.size();
}

[[nodiscard]] std::uint64_t read_little_endian(const group::SessionState& state,
                                               std::size_t offset,
                                               std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= std::to_integer<std::uint64_t>(state[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] bool
expect(std::uint64_t actual, std::uint64_t expected, const char* field) noexcept {
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr,
                 "%s mismatch: actual=0x%llX expected=0x%llX\n",
                 field,
                 static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    return false;
}

[[nodiscard]] bool profile_region_is_canonical_base(const group::SessionState& state,
                                                    std::size_t entry) noexcept {
    for (std::size_t offset = 0; offset < kPlayerStride; ++offset) {
        const bool baseField = (offset >= 0x04 && offset < 0x0C)
                               || (offset >= 0x0C && offset < 0x20)
                               || (offset >= 0x108 && offset < 0x10C);
        if (!baseField && state[entry + offset] != std::byte{0}) {
            std::fprintf(
                stderr, "optional profile byte was populated at player offset 0x%zX\n", offset);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool encoded_players_omit_profiles(const group::MembershipUpdate& update) noexcept {
    std::array<std::byte, 1024> storage{};
    dawn::middleware::encoding::bits::Writer writer(storage);
    std::size_t encodedSize = 0;
    if (!group::write_membership_update(writer, update) || !writer.finish(encodedSize)) {
        std::fputs("membership update failed to encode\n", stderr);
        return false;
    }

    dawn::middleware::encoding::bits::Reader reader(
        std::span<const std::byte>(storage.data(), encodedSize));
    std::uint64_t value = 0;
    if (!reader.skip(64) || !reader.read(13, value)) {
        return false;
    }
    std::array<std::byte, 5972> protobufStorage{};
    const std::size_t protobufBytes = static_cast<std::size_t>(value);
    if (protobufBytes > protobufStorage.size()
        || !dawn::middleware::encoding::bits::read_raw(
            reader, std::span(protobufStorage).first(protobufBytes))
        || !protobuf_members_publish_player_counts(
            std::span<const std::byte>(protobufStorage.data(), protobufBytes), update.members)) {
        return false;
    }
    std::uint64_t memberCount = 0;
    std::uint64_t playerCount = 0;
    if (!reader.skip(1 + 32 + 32) || !reader.read(6, memberCount) || !reader.read(6, playerCount)
        || memberCount != update.members.size() || playerCount != update.players.size()) {
        std::fputs("membership update count prefix mismatch\n", stderr);
        return false;
    }

    for (const group::MembershipMember& member : update.members) {
        if (!reader.skip(6 + 1 + 4 + 1)) {
            return false;
        }
        if (member.connectionPresent && !reader.skip(32 + 64 + 8)) {
            return false;
        }
        if (!reader.skip(3)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < update.players.size(); ++index) {
        std::uint64_t profilePresent = 1;
        // slot, full-entry flag, identity-present flag, raw player id, member, owned-local index,
        // add sequence, player flag, then the optional-profile discriminator
        if (!reader.skip(5 + 1 + 1 + 64 + 6 + 1 + 20 + 1) || !reader.read(1, profilePresent)
            || profilePresent != 0) {
            std::fprintf(stderr, "player %zu encoded an optional membership profile\n", index);
            return false;
        }
    }

    const std::size_t tailBits = update.playerAdditionNumberPresent
                                     ? 1 + group::kPlayerAdditionNumberWidth + 3
                                     : 4;
    std::uint64_t encodedHash = 0;
    return reader.skip(tailBits)
           && reader.read(32, encodedHash)
           && encodedHash == group::session_state_hash(update)
           && reader.remaining_bits() < 8;
}

} // namespace

int main() {
    static_assert(group::kSessionStateSize == 0x7060);

    std::array<group::MembershipMember, 2> members{};
    members[0].machineId = 0x1111222233334444ULL;
    members[1].machineId = 0x5555666677778888ULL;
    members[0].ownsPlayerSlot = true;
    members[0].playerSlot = 0;
    members[1].ownsPlayerSlot = true;
    members[1].playerSlot = 1;

    std::array<group::MembershipPlayer, 2> players{};
    players[0].slot = 0;
    players[0].playerId = 0x0110000100000101ULL;
    players[0].memberIndex = 0;
    players[0].addSequence = 0;
    players[0].profilePresent = false;
    players[1].slot = 1;
    players[1].playerId = 0x0110000100000001ULL;
    players[1].memberIndex = 1;
    players[1].addSequence = 1;
    players[1].profilePresent = false;

    group::MembershipUpdate update{};
    update.hostMachineId = members[0].machineId;
    update.revision = 1;
    update.hostMemberIndex = 0;
    update.successionIndex = 0;
    update.members = members;
    update.players = players;
    update.nextPlayerAdditionNumber = 2;

    group::SessionState state{};
    group::build_session_state(update, state);

    bool ok = encoded_players_omit_profiles(update);
    ok &= expect(read_little_endian(state, kPlayerCountOffset, 4), 2, "player count");
    ok &= expect(read_little_endian(state, kPlayerMaskOffset, 4), 3, "player mask");
    ok &= expect(read_little_endian(state, kPlayerAdditionNumberOffset, 4),
                 update.nextPlayerAdditionNumber,
                 "next player addition number");

    for (std::size_t index = 0; index < members.size(); ++index) {
        const std::size_t entry = kMemberTableOffset + index * kMemberStride;
        ok &= expect(read_little_endian(state, entry + 0xA0, 1), 0, "player count has byte");
        ok &= expect(read_little_endian(state, entry + 0xA4, 4), 1, "member player count");
        ok &= expect(read_little_endian(state, entry + 0xA8, 8), 1, "player slot count");
        ok &= expect(read_little_endian(state, entry + 0xB0, 4), index, "member player slot");
    }

    for (std::size_t index = 0; index < players.size(); ++index) {
        const std::size_t entry = kPlayerTableOffset + index * kPlayerStride;
        ok &= expect(
            read_little_endian(state, entry + 0x04, 8), players[index].playerId, "player id");
        ok &= expect(
            read_little_endian(state, entry + 0x0C, 4), players[index].memberIndex, "member index");
        ok &= expect(read_little_endian(state, entry + 0x10, 4), 0, "owned index");
        ok &= expect(
            read_little_endian(state, entry + 0x14, 4), players[index].addSequence, "add sequence");
        ok &= expect(read_little_endian(state, entry + 0x18, 1), 0, "player flag");
        ok &= expect(read_little_endian(state, entry + 0x1C, 4), 0xFFFFFFFFULL, "base default A");
        ok &= expect(read_little_endian(state, entry + 0x108, 4), 0xFFFFFFFFULL, "base default B");
        ok &= profile_region_is_canonical_base(state, entry);
    }

    auto publicMembers = members;
    for (group::MembershipMember& member : publicMembers) {
        member.ownedPlayerCountPresent = false;
    }
    group::MembershipUpdate publicUpdate = update;
    publicUpdate.members = publicMembers;
    publicUpdate.nextPlayerAdditionNumber = 7;
    publicUpdate.playerAdditionNumberPresent = false;
    group::SessionState publicState{};
    group::build_session_state(publicUpdate, publicState);
    ok &= encoded_players_omit_profiles(publicUpdate);
    ok &= expect(read_little_endian(publicState, kPlayerAdditionNumberOffset, 4),
                 0,
                 "public omitted addition number");
    for (std::size_t index = 0; index < publicMembers.size(); ++index) {
        const std::size_t entry = kMemberTableOffset + index * kMemberStride;
        ok &= expect(read_little_endian(publicState, entry + 0xA4, 4),
                     0,
                     "public omitted scalar player count");
        ok &= expect(read_little_endian(publicState, entry + 0xA8, 8),
                     1,
                     "public player slot count");
        ok &= expect(read_little_endian(publicState, entry + 0xB0, 4),
                     index,
                     "public member player slot");
    }

    const auto selfBefore=publicMembers[0];
    constexpr char steamAddress[]="steamid:76560008000000002#0123456789abcdef";
    for(std::size_t i=0;i<sizeof(steamAddress);++i)
        publicMembers[1].address[i]=static_cast<std::byte>(steamAddress[i]);
    publicMembers[1].address.back()=std::byte{6};
    publicMembers[1].connectionPresent=true;
    publicMembers[1].joinCompatibility=0x1234;
    publicMembers[1].joinTimestamp=0x1122334455667788ULL;
    publicMembers[1].connectionValue=3;
    group::build_session_state(publicUpdate,publicState);
    ok &= encoded_players_omit_profiles(publicUpdate);
    for(std::size_t i=0;i<publicMembers[1].address.size();++i)
        ok &= expect(read_little_endian(publicState,kMemberTableOffset+kMemberStride+16+i,1),
            std::to_integer<unsigned>(publicMembers[1].address[i]),"public Steam address preserved exactly");
    ok &= publicMembers[0].address==selfBefore.address
        && publicMembers[0].machineId==selfBefore.machineId
        && publicMembers[0].connectionPresent==selfBefore.connectionPresent;

    if (!ok) {
        return 1;
    }

    std::printf("PASS: PC 1.59 no-profile membership rows match the 0x7060 native base layout\n");
    return 0;
}
