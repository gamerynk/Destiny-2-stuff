#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "middleware/encoding/bit_raw.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/gameplay/group/member_messages.h"
#include "middleware/gameplay/group/session_messages.h"
#include "middleware/gameplay/group/session_state.h"

namespace {

namespace bits = dawn::middleware::encoding::bits;
namespace group = dawn::middleware::gameplay::group;

constexpr std::size_t kPlayerTableOffset = 0x3B58;
constexpr std::size_t kPlayerEmblemOffset = 0x00;
constexpr std::size_t kPlayerSequenceOffset = 0x14;
constexpr std::size_t kPlayerProfileTypeOffset = 0x1C;
constexpr std::size_t kPlayerDisplayNameOffset = 0x20;
constexpr std::size_t kPlayerIdentityOffset = 0x20 + 0x80;
constexpr std::size_t kPlayerCodecAField2Offset = 0x20 + 0xB0;
constexpr std::size_t kPlayerCodecAField3Offset = 0x20 + 0xB2;
constexpr std::size_t kPlayerCodecAField8Offset = 0x20 + 0xB3;
constexpr std::size_t kPlayerCodecAField4Offset = 0x20 + 0xB8;
constexpr std::size_t kPlayerCodecAField5Offset = 0x20 + 0xC0;
constexpr std::size_t kPlayerCodecAField6Offset = 0x20 + 0xD0;
constexpr std::size_t kPlayerLightOffset = 0x20 + 0xE0;
constexpr std::size_t kPlayerCodecBOffset = 0x108;
constexpr std::size_t kPlayerProfileTailOffset = 0x190;
constexpr std::size_t kPlayerStride = 0x1A8;

[[nodiscard]] constexpr std::uint8_t hex_nibble(char value) noexcept {
    return value >= '0' && value <= '9' ? static_cast<std::uint8_t>(value - '0')
           : value >= 'A' && value <= 'F' ? static_cast<std::uint8_t>(value - 'A' + 10)
                                          : static_cast<std::uint8_t>(value - 'a' + 10);
}

template <std::size_t Size>
[[nodiscard]] constexpr auto decode_hex(const char (&value)[Size]) noexcept {
    static_assert((Size - 1) % 2 == 0);
    std::array<std::byte, (Size - 1) / 2> output{};
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::byte>((hex_nibble(value[index * 2]) << 4U)
                                               | hex_nibble(value[index * 2 + 1]));
    }
    return output;
}

constexpr auto kSyntheticPlayerIdentity =
    decode_hex("010000000100100100000000000000000000000000000000000000000000000000000000");
static_assert(kSyntheticPlayerIdentity.size() == 36);

// PC 1.59 reader output for the wire sequence PlayerOne\0
constexpr std::array<std::uint16_t, 10> kPlayerOneStoredName{
0x88B0, 0x64DD, 0x7BFC, 0xCE71, 0xAA67, 0xF1B6, 0x3C51, 0x9993, 0x62E9, 0x8984};

[[nodiscard]] constexpr group::PlayerProfileSupplement live_profile_supplement() noexcept {
    group::PlayerProfileSupplement profile{};
    profile.field2Present = true;
    profile.field2 = 0xBD;
    profile.field3Present = true;
    profile.field3 = 0xFF;
    profile.field8Present = true;
    profile.field8 = 0xFF;
    profile.field4Present = true;
    profile.field4 = {0, 0};
    profile.field5Present = true;
    profile.field5 = {0x9EAA000100100100ULL, 0x9EAA000100100101ULL};
    profile.field6Present = true;
    profile.field6FloatBits = 0x44848000U;
    profile.field6Signed = {-1, -1, -1, -1};
    profile.field6Byte = 0;
    profile.field7Present = true;
    profile.field7 = 12;
    return profile;
}

[[nodiscard]] constexpr bool account_profile_supplement_is_exact() noexcept {
    constexpr std::uint64_t kPrimarySoid = 0x9EAA000100200100ULL;
    constexpr std::uint64_t kCharacterSoid = 0x9EAA000100200102ULL;
    constexpr auto profile = group::make_account_membership_profile_supplement(
        kPrimarySoid, kCharacterSoid, 0x44848000U);
    return profile.field2Present && profile.field2 == 0xBD && profile.field3Present
           && profile.field3 == 0xFF && profile.field8Present && profile.field8 == 0xFF
           && profile.field4Present && profile.field4 == std::array<std::int32_t, 2>{0, 0}
           && profile.field5Present
           && profile.field5 == std::array<std::uint64_t, 2>{kPrimarySoid, kCharacterSoid}
           && profile.field6Present && profile.field6FloatBits == 0x44848000U
           && profile.field6Signed == std::array<std::int16_t, 4>{-1, -1, -1, -1}
           && profile.field6Byte == 0 && profile.field7Present && profile.field7 == 12
           && profile.tailWords == std::array<std::uint32_t, 3>{0, 0, 0}
           && profile.tailFive == 1 && profile.tailTwo == 1 && !profile.tailFlag
           && !profile.tailOptionalPresent;
}

static_assert(account_profile_supplement_is_exact());

[[nodiscard]] std::uint64_t read_le(const group::SessionState& state,
                                    std::size_t offset,
                                    std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= std::to_integer<std::uint64_t>(state[offset + index]) << (index * 8U);
    }
    return value;
}

template <std::size_t Size>
[[nodiscard]] std::uint64_t read_le_prefix(const std::array<std::byte, Size>& bytes) noexcept {
    static_assert(Size >= sizeof(std::uint64_t));
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint32_t rotate_left_32(std::uint32_t value,
                                                     unsigned shift) noexcept {
    shift &= 31U;
    return shift == 0 ? value : (value << shift) | (value >> (32U - shift));
}

[[nodiscard]] constexpr std::uint16_t native_state_name_unit(std::uint16_t wire,
                                                             std::size_t index) noexcept {
    const std::uint16_t mask =
        index == 0
            ? 0
            : static_cast<std::uint16_t>(
                  rotate_left_32(0xC245B0C4U, static_cast<unsigned>(index % 31U)));
    return static_cast<std::uint16_t>(
               static_cast<std::uint32_t>(wire) * std::uint16_t{0x7B4F})
           ^ mask;
}

[[nodiscard]] bool encoded_name_is_exact(bits::Reader& reader,
                                         const group::MembershipPlayer& player) noexcept {
    for (std::size_t index = 0; index < player.displayName.size(); ++index) {
        std::uint64_t unit = 0;
        const std::uint16_t expected = static_cast<std::uint16_t>(player.displayName[index]);
        if (!reader.read(16, unit) || unit != expected) {
            return false;
        }
        if (unit == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool state_name_is_exact(const group::SessionState& state,
                                       std::size_t playerSlot,
                                       const group::MembershipPlayer& player) noexcept {
    const std::size_t name = kPlayerTableOffset + kPlayerStride * playerSlot
                             + kPlayerDisplayNameOffset;
    bool terminated = false;
    for (std::size_t index = 0; index < player.displayName.size(); ++index) {
        const auto wire = static_cast<std::uint16_t>(player.displayName[index]);
        const std::uint16_t expected =
            terminated ? 0 : native_state_name_unit(wire, index);
        if (read_le(state, name + index * sizeof(char16_t), sizeof(char16_t))
            != expected) {
            return false;
        }
        terminated = terminated || wire == 0;
    }
    return terminated;
}

[[nodiscard]] bool encoded_profile_is_exact(const group::MembershipUpdate& update) noexcept {
    const auto fail = [](const char* stage) noexcept {
        std::fprintf(stderr, "ID30 profile mismatch at %s\n", stage);
        return false;
    };
    std::array<std::byte, 1024> storage{};
    bits::Writer writer(storage);
    if (!group::write_membership_update(writer, update)) {
        return fail("write");
    }
    const std::size_t meaningfulBits = writer.bit_count();
    std::size_t encodedBytes = 0;
    if (!writer.finish(encodedBytes)) {
        return fail("finish");
    }
    bits::Reader reader(std::span<const std::byte>(storage.data(), encodedBytes), meaningfulBits);
    std::uint64_t protobufBytes = 0;
    std::uint64_t memberCount = 0;
    std::uint64_t playerCount = 0;
    if (!reader.skip(64) || !reader.read(13, protobufBytes)
        || !reader.skip(static_cast<std::size_t>(protobufBytes) * 8U)
        || !reader.skip(1 + 32 + 32) || !reader.read(6, memberCount)
        || !reader.read(6, playerCount) || memberCount != update.members.size()
        || playerCount != 1) {
        return fail("root");
    }
    for (const group::MembershipMember& member : update.members) {
        if (!reader.skip(6 + 1 + 4 + 1)
            || (member.connectionPresent && !reader.skip(32 + 64 + 8)) || !reader.skip(3)) {
            return fail("member");
        }
    }

    std::uint64_t value = 0;
    std::uint64_t playerId = 0;
    if (!reader.read(5, value) || value != update.players[0].slot || !reader.skip(1 + 1)
        || !bits::read_raw_u64(reader, playerId) || playerId != update.players[0].playerId
        || !reader.read(6, value) || value != update.players[0].memberIndex
        || !reader.skip(1 + 20 + 1) || !reader.read(1, value) || value != 1
        || !reader.read(32, value) || value != update.players[0].emblemDefinitionIndex
        || !reader.read(3, value) || value != 1) {
        return fail("player-header");
    }
    std::array<std::byte, 36> encodedIdentity{};
    const auto& profile = update.players[0].profileSupplement;
    if (!reader.read(1, value) || value != 1
        || !encoded_name_is_exact(reader, update.players[0])
        || !reader.read(1, value) || value != 1
        || !bits::read_raw(reader, encodedIdentity) || encodedIdentity != kSyntheticPlayerIdentity
        || !reader.read(1, value) || value != 1
        || !reader.read(8, value) || value != profile.field2
        || !reader.read(1, value) || value != 1
        || !reader.read(6, value) || value != 0
        || !reader.read(1, value) || value != 1
        || !reader.read(6, value) || value != 0
        || !reader.read(1, value) || value != 1) {
        return fail("codec-a-0-through-4");
    }
    for (const std::int32_t component : profile.field4) {
        if (!reader.read(16, value)
            || value != static_cast<std::uint16_t>(component)) {
            return fail("codec-a-field4");
        }
    }
    if (!reader.read(1, value) || value != 1) {
        return fail("codec-a-field5-presence");
    }
    for (const std::uint64_t component : profile.field5) {
        if (!reader.read(64, value) || value != component) {
            return fail("codec-a-field5");
        }
    }
    if (!reader.read(1, value) || value != 1
        || !reader.read(32, value) || value != profile.field6FloatBits) {
        return fail("codec-a-field6-header");
    }
    for (const std::int16_t component : profile.field6Signed) {
        const auto expected = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(component) + 32768);
        if (!reader.read(16, value) || value != expected) {
            return fail("codec-a-field6-signed");
        }
    }
    if (!reader.read(8, value) || value != profile.field6Byte
        || !reader.read(1, value) || value != 1
        || !reader.read(32, value)
        || value != static_cast<std::uint32_t>(update.players[0].light)) {
        return fail("codec-a-field6-byte-and-field7");
    }
    for (std::size_t field = 0; field < 4; ++field) {
        if (!reader.read(1, value) || value != 0) {
            return fail("codec-b-clear-presence");
        }
    }
    for (const std::uint32_t word : profile.tailWords) {
        if (!reader.read(32, value) || value != word) {
            return fail("profile-tail-words");
        }
    }
    if (!reader.read(5, value) || value != profile.tailFive) {
        return fail("profile-tail-five");
    }
    if (!reader.read(2, value) || value != profile.tailTwo) {
        return fail("profile-tail-two");
    }
    if (!reader.read(1, value) || value != static_cast<std::uint64_t>(profile.tailFlag)) {
        return fail("profile-tail-flag");
    }
    if (!reader.skip(1 + group::kPlayerAdditionNumberWidth + 3) || !reader.read(32, value)) {
        return fail("state-hash-read");
    }
    if (value != group::session_state_hash(update)) {
        return fail("state-hash-value");
    }
    if (reader.remaining_bits() != 0) {
        return fail("trailing-bits");
    }
    return true;
}

[[nodiscard]] bool two_account_projection_is_exact() noexcept {
    constexpr std::uint64_t kOwnerPlatformId = 0x0110000100000101ULL;
    std::array<group::MembershipMember, 2> members{};
    members[0].ownsPlayerSlot = true;
    members[0].playerSlot = 0;
    members[1].ownsPlayerSlot = true;
    members[1].playerSlot = 1;

    std::array<group::MembershipPlayer, 2> players{};
    players[0].slot = 0;
    players[0].playerId = kOwnerPlatformId;
    players[0].memberIndex = 0;
    // membership carries one session-wide add generation per player; the ID34 sequence is
    // transport history and must not replace this 0..N counter
    players[0].addSequence = 0;
    players[0].profilePresent = true;
    players[0].emblemDefinitionIndex = 1272;
    players[0].light = 1060;
    players[0].profileSupplement = group::make_account_membership_profile_supplement(
        0x9EAA000100200100ULL, 0x9EAA000100200102ULL, 0x44848000U);
    if (!group::set_ascii_membership_display_name(players[0], "PlayerTwo")
        || !group::set_synthesized_pc_platform_identity(players[0], kOwnerPlatformId)) {
        return false;
    }

    players[1].slot = 1;
    players[1].playerId = 0x0102030405060708ULL;
    players[1].memberIndex = 1;
    players[1].addSequence = 1;
    players[1].profilePresent = true;
    players[1].emblemDefinitionIndex = 1273;
    players[1].light = 1060;
    players[1].identityPresent = true;
    players[1].identity = kSyntheticPlayerIdentity;
    players[1].profileSupplement = group::make_account_membership_profile_supplement(
        0x9EAA000100100100ULL, 0x9EAA000100100101ULL, 0x44848000U);
    if (!group::set_ascii_membership_display_name(players[1], "PlayerOne")) {
        return false;
    }

    group::MembershipUpdate update{};
    update.hostMachineId = 0x1111222233334444ULL;
    update.revision = 3;
    update.members = members;
    update.players = players;

    group::SessionState state{};
    group::build_session_state(update, state);
    bool ownerIdentityExact = true;
    for (std::size_t byte = 0; byte < players[0].identity.size(); ++byte) {
        ownerIdentityExact &=
            state[kPlayerTableOffset + kPlayerIdentityOffset + byte] == players[0].identity[byte];
    }
    bool joinerIdentityExact = true;
    for (std::size_t byte = 0; byte < players[1].identity.size(); ++byte) {
        joinerIdentityExact &= state[kPlayerTableOffset + kPlayerStride + kPlayerIdentityOffset
                                     + byte]
                               == players[1].identity[byte];
    }
    return ownerIdentityExact && joinerIdentityExact
           && state_name_is_exact(state, 0, players[0])
           && state_name_is_exact(state, 1, players[1])
           && read_le(state, kPlayerTableOffset + kPlayerSequenceOffset, 4) == 0
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerSequenceOffset,
                      4)
                  == 1
           && read_le(state, kPlayerTableOffset + kPlayerEmblemOffset, 4) == 1272
           && read_le(state, kPlayerTableOffset + kPlayerLightOffset, 4) == 1060
           && read_le(state, kPlayerTableOffset + kPlayerStride + kPlayerEmblemOffset, 4) == 1273
           && read_le(state, kPlayerTableOffset + kPlayerStride + kPlayerLightOffset, 4) == 1060
           && read_le(state, kPlayerTableOffset + kPlayerCodecAField5Offset, 8)
                  == players[0].profileSupplement.field5[0]
           && read_le(state, kPlayerTableOffset + kPlayerCodecAField5Offset + 8, 8)
                  == players[0].profileSupplement.field5[1]
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerCodecAField5Offset,
                      8)
                  == players[1].profileSupplement.field5[0]
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerCodecAField5Offset + 8,
                      8)
                  == players[1].profileSupplement.field5[1]
           && read_le(state, kPlayerTableOffset + kPlayerProfileTailOffset + 0x0C, 1) == 1
           && read_le(state, kPlayerTableOffset + kPlayerProfileTailOffset + 0x0D, 1) == 1
           && read_le(state, kPlayerTableOffset + kPlayerCodecBOffset, 4) == 0
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerCodecBOffset,
                      4)
                  == 0
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerProfileTailOffset + 0x0C,
                      1)
                  == 1
           && read_le(state,
                      kPlayerTableOffset + kPlayerStride + kPlayerProfileTailOffset + 0x0D,
                      1)
                  == 1;
}

[[nodiscard]] bool synthetic_platform_identity_is_exact() noexcept {
    group::MembershipPlayer player{};
    return group::set_synthesized_pc_platform_identity(player, 0x0110000100000001ULL)
        && player.identity == kSyntheticPlayerIdentity
        && read_le_prefix(player.identity) == 0x0110000100000001ULL;
}

[[nodiscard]] bool boot_machine_codec_is_exact() noexcept {
    constexpr std::uint64_t kSession = 0x010203040506070AULL;
    constexpr std::uint8_t kKind = 3;
    constexpr std::uint8_t kMethod = 6;
    std::array<std::byte, dawn::middleware::gameplay::descriptor::kNetAddrSize> address{};
    for (std::size_t index = 0; index < address.size() - 1; ++index) {
        address[index] = static_cast<std::byte>((index * 17U + 9U) & 0xFFU);
    }
    address.back() = static_cast<std::byte>(kMethod);

    std::array<std::byte, 128> storage{};
    bits::Writer writer(storage);
    if (!bits::write_raw_u64(writer, kSession) || !writer.write(kKind, 3)
        || !writer.write(kMethod, 3)
        || !bits::write_raw(writer,
                            std::span<const std::byte>(address.data(), address.size() - 1))) {
        return false;
    }
    const std::size_t meaningfulBits = writer.bit_count();
    std::size_t encodedBytes = 0;
    if (!writer.finish(encodedBytes) || meaningfulBits != 750) {
        return false;
    }

    bits::Reader reader(
        std::span<const std::byte>(storage.data(), encodedBytes), meaningfulBits);
    group::BootMachineRequest request{};
    return group::read_boot_machine(reader, request)
           && request.sessionId == kSession && request.kind == kKind
           && request.address == address && reader.remaining_bits() == 0;
}

[[nodiscard]] bool display_name_capacity_is_exact() noexcept {
    std::array<char, group::kMembershipDisplayNameCapacity - 1> longest{};
    longest.fill('A');
    std::array<char, group::kMembershipDisplayNameCapacity> tooLong{};
    tooLong.fill('B');

    group::MembershipPlayer accepted{};
    group::MembershipPlayer rejected{};
    if (!group::set_ascii_membership_display_name(
            accepted, std::string_view(longest.data(), longest.size()))
        || accepted.displayName.back() != 0
        || group::set_ascii_membership_display_name(
            rejected, std::string_view(tooLong.data(), tooLong.size()))) {
        return false;
    }

    std::array<group::MembershipMember, 1> members{};
    members[0].ownsPlayerSlot = true;
    std::array<group::MembershipPlayer, 1> players{};
    players[0].playerId = 1;
    players[0].profilePresent = true;
    players[0].displayNamePresent = true;
    players[0].displayName.fill(u'X');
    group::MembershipUpdate malformed{};
    malformed.hostMachineId = 1;
    malformed.revision = 1;
    malformed.members = members;
    malformed.players = players;
    std::array<std::byte, 2048> storage{};
    bits::Writer writer(storage);
    return !group::write_membership_update(writer, malformed);
}

} // namespace

int main() {
    std::array<group::MembershipMember, 2> members{};
    members[1].ownsPlayerSlot = true;
    members[1].playerSlot = 0;

    std::array<group::MembershipPlayer, 1> players{};
    players[0].slot = 0;
    players[0].playerId = 0xA9E15B83AEFCF837ULL;
    players[0].memberIndex = 1;
    players[0].profilePresent = true;
    players[0].emblemDefinitionIndex = 1273;
    players[0].profileType = 0;
    players[0].light = 1060;
    if (!group::set_ascii_membership_display_name(players[0], "PlayerOne")) {
        std::fputs("managed membership display-name setup failed\n", stderr);
        return 1;
    }
    players[0].identityPresent = true;
    players[0].identity = kSyntheticPlayerIdentity;
    players[0].profileSupplement = live_profile_supplement();

    group::MembershipUpdate update{};
    update.hostMachineId = 0x1111222233334444ULL;
    update.revision = 1;
    update.members = members;
    update.players = players;

    group::SessionState state{};
    group::build_session_state(update, state);
    bool identityStateExact = true;
    for (std::size_t byte = 0; byte < kSyntheticPlayerIdentity.size(); ++byte) {
        identityStateExact &= state[kPlayerTableOffset + kPlayerIdentityOffset + byte]
                              == kSyntheticPlayerIdentity[byte];
    }
    bool goldenNameStateExact = true;
    for (std::size_t unit = 0; unit < kPlayerOneStoredName.size(); ++unit) {
        goldenNameStateExact &=
            read_le(state,
                    kPlayerTableOffset + kPlayerDisplayNameOffset + unit * sizeof(char16_t),
                    sizeof(char16_t))
            == kPlayerOneStoredName[unit];
    }
    const bool syntheticIdentityExact = synthetic_platform_identity_is_exact();
    const bool bootMachineExact = boot_machine_codec_is_exact();
    const bool displayNameCapacityExact = display_name_capacity_is_exact();
    const bool encodedProfileExact = encoded_profile_is_exact(update);
    const bool twoAccountProjectionExact = two_account_projection_is_exact();
    const bool ok = syntheticIdentityExact && bootMachineExact
                    && displayNameCapacityExact
                    && encodedProfileExact
                    && identityStateExact && goldenNameStateExact
                    && state_name_is_exact(state, 0, players[0])
                    && twoAccountProjectionExact
                    && read_le(state, kPlayerTableOffset + kPlayerEmblemOffset, 4) == 1273
                    && read_le(state, kPlayerTableOffset + kPlayerProfileTypeOffset, 4) == 0
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField2Offset, 2)
                           == 0xBD
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField3Offset, 1)
                           == 0xFF
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField8Offset, 1)
                           == 0xFF
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField4Offset, 8) == 0
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField5Offset, 8)
                           == 0x9EAA000100100100ULL
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField5Offset + 8, 8)
                           == 0x9EAA000100100101ULL
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField6Offset, 4)
                           == 0x44848000U
                    && read_le(state, kPlayerTableOffset + kPlayerCodecAField6Offset + 4, 8)
                           == 0xFFFFFFFFFFFFFFFFULL
                    && read_le(state, kPlayerTableOffset + kPlayerCodecBOffset, 4) == 0
                    && read_le(state, kPlayerTableOffset + kPlayerProfileTailOffset, 4) == 0
                    && read_le(state, kPlayerTableOffset + kPlayerLightOffset, 4) == 1060;
    if (!ok) {
        std::fprintf(stderr,
                     "subcontracts: id34=%d boot=%d capacity=%d id30=%d identity-state=%d "
                     "golden-name=%d two-account=%d\n",
                     syntheticIdentityExact,
                     bootMachineExact,
                     displayNameCapacityExact,
                     encodedProfileExact,
                     identityStateExact,
                     goldenNameStateExact,
                     twoAccountProjectionExact);
        std::fputs("managed membership profile contract failed\n", stderr);
        return 1;
    }
    std::puts("PASS: PC 1.59 managed membership profile wire/state contract");
    return 0;
}
