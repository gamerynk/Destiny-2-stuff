#include "member_messages.h"

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::group {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kMethodWidth = 3;
constexpr std::uint8_t kBootKindWidth = 3;
constexpr std::size_t kShortAddressBytes = 41;
constexpr std::size_t kLongAddressBytes = 85;
constexpr std::uint64_t kFirstLongMethod = 6;
constexpr std::uint8_t kReservedWidth = 1;
constexpr std::uint8_t kSequenceWidth = 32;
constexpr std::uint8_t kKindWidth = 2;
// the native player-remove codec bounds the local-player index to two slots
constexpr std::uint8_t kPlayerIndexWidth = 1;
constexpr std::uint8_t kPresenceWidth = 1;
constexpr std::size_t kPlayerNameUnitCapacity = 64;
constexpr std::uint8_t kUtf16UnitWidth = 16;
constexpr std::uint8_t kCodecAField2Width = 8;
constexpr std::uint8_t kCodecASixBitWidth = 6;
constexpr std::uint8_t kCodecAField4ComponentWidth = 16;
constexpr std::uint8_t kCodecAField5ComponentWidth = 64;
constexpr std::uint8_t kCodecAField6FloatWidth = 32;
constexpr std::uint8_t kCodecAField6SignedWidth = 16;
constexpr std::uint8_t kCodecAField6ByteWidth = 8;
constexpr std::uint8_t kCodecAField7Width = 32;
constexpr std::int32_t kCodecAField6SignedMinimum = -32768;
constexpr std::uint8_t kProfileTailWordWidth = 32;
constexpr std::uint8_t kProfileTailFiveWidth = 5;
constexpr std::uint8_t kProfileTailTwoWidth = 2;
constexpr std::uint8_t kProfileTailFlagWidth = 1;
constexpr std::uint8_t kProfileTailOptionalBit = 4;

} // namespace

bool read_peer_properties_header(bits::Reader& reader, PeerPropertiesHeader& output) noexcept {
    PeerPropertiesHeader candidate{};
    std::uint64_t method = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId) || !reader.read(kMethodWidth, method)) {
        return false;
    }
    const std::size_t addressBytes =
        method >= kFirstLongMethod ? kLongAddressBytes : kShortAddressBytes;
    if (!bits::skip_raw(reader, addressBytes)) {
        return false;
    }
    candidate.addressMethod = static_cast<std::uint8_t>(method);
    output = candidate;
    return true;
}

bool read_player_add(bits::Reader& reader, PlayerAddRequest& output) noexcept {
    PlayerAddRequest candidate{};
    std::uint64_t reserved = 0;
    std::uint64_t sequence = 0;
    std::uint64_t kind = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId) || !reader.read(kReservedWidth, reserved)
        || !bits::read_raw_u64(reader, candidate.playerId) || !reader.read(kSequenceWidth, sequence)
        || !reader.read(kKindWidth, kind)) {
        return false;
    }
    // the consumer refuses a set reserved bit
    if (reserved != 0) {
        return false;
    }
    std::uint64_t namePresent = 0;
    if (!reader.read(kPresenceWidth, namePresent)) {
        return false;
    }
    if (namePresent != 0) {
        bool terminated = false;
        for (std::size_t index = 0; index < kPlayerNameUnitCapacity; ++index) {
            std::uint64_t unit = 0;
            if (!reader.read(kUtf16UnitWidth, unit)) {
                return false;
            }
            if (unit == 0) {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            return false;
        }
    }

    std::uint64_t identityPresent = 0;
    if (!reader.read(kPresenceWidth, identityPresent)
        || (identityPresent != 0 && !bits::read_raw(reader, candidate.identity))) {
        return false;
    }

    auto& profile = candidate.profileSupplement;
    std::uint64_t present = 0;
    std::uint64_t value = 0;
    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field2Present = present != 0;
    if (profile.field2Present) {
        if (!reader.read(kCodecAField2Width, value)) {
            return false;
        }
        profile.field2 = static_cast<std::uint16_t>(value);
    }

    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field3Present = present != 0;
    if (profile.field3Present) {
        if (!reader.read(kCodecASixBitWidth, value)) {
            return false;
        }
        profile.field3 = static_cast<std::uint8_t>(value - 1U);
    }

    // native field 8 precedes fields 4 through 7 on this wire
    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field8Present = present != 0;
    if (profile.field8Present) {
        if (!reader.read(kCodecASixBitWidth, value)) {
            return false;
        }
        profile.field8 = static_cast<std::uint8_t>(value - 1U);
    }

    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field4Present = present != 0;
    if (profile.field4Present) {
        for (std::int32_t& component : profile.field4) {
            if (!reader.read(kCodecAField4ComponentWidth, value)) {
                return false;
            }
            const auto encoded = static_cast<std::uint16_t>(value);
            component = (encoded & 0x8000U) != 0
                            ? static_cast<std::int32_t>(encoded) - 0x10000
                            : static_cast<std::int32_t>(encoded);
        }
    }

    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field5Present = present != 0;
    if (profile.field5Present) {
        for (std::uint64_t& component : profile.field5) {
            if (!reader.read(kCodecAField5ComponentWidth, component)) {
                return false;
            }
        }
    }

    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field6Present = present != 0;
    if (profile.field6Present) {
        if (!reader.read(kCodecAField6FloatWidth, value)) {
            return false;
        }
        profile.field6FloatBits = static_cast<std::uint32_t>(value);
        for (std::int16_t& component : profile.field6Signed) {
            if (!reader.read(kCodecAField6SignedWidth, value)) {
                return false;
            }
            const std::int32_t decoded =
                static_cast<std::int32_t>(static_cast<std::uint16_t>(value))
                + kCodecAField6SignedMinimum;
            component = static_cast<std::int16_t>(decoded);
        }
        if (!reader.read(kCodecAField6ByteWidth, value)) {
            return false;
        }
        profile.field6Byte = static_cast<std::uint8_t>(value);
    }

    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    profile.field7Present = present != 0;
    if (profile.field7Present) {
        if (!reader.read(kCodecAField7Width, value)) {
            return false;
        }
        profile.field7 = static_cast<std::uint32_t>(value);
    }

    for (std::uint32_t& word : profile.tailWords) {
        if (!reader.read(kProfileTailWordWidth, value)) {
            return false;
        }
        word = static_cast<std::uint32_t>(value);
    }
    if (!reader.read(kProfileTailFiveWidth, value)) {
        return false;
    }
    profile.tailFive = static_cast<std::uint8_t>(value);
    if (!reader.read(kProfileTailTwoWidth, value)) {
        return false;
    }
    profile.tailTwo = static_cast<std::uint8_t>(value);
    if (!reader.read(kProfileTailFlagWidth, value)) {
        return false;
    }
    profile.tailFlag = value != 0;
    if ((profile.tailFive & (1U << kProfileTailOptionalBit)) != 0) {
        if (!reader.read(kPresenceWidth, present)) {
            return false;
        }
        profile.tailOptionalPresent = present != 0;
        if (profile.tailOptionalPresent) {
            if (!reader.read(kProfileTailFiveWidth, value)) {
                return false;
            }
            profile.tailOptionalFive = static_cast<std::uint8_t>(value);
        }
    }
    candidate.sequence = static_cast<std::uint32_t>(sequence);
    candidate.kind = static_cast<std::uint8_t>(kind);
    candidate.identityPresent = identityPresent != 0;
    output = candidate;
    return true;
}

bool read_boot_machine(bits::Reader& reader, BootMachineRequest& output) noexcept {
    BootMachineRequest candidate{};
    std::uint64_t kind = 0;
    std::uint64_t method = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId)
        || !reader.read(kBootKindWidth, kind) || !reader.read(kMethodWidth, method)) {
        return false;
    }
    const std::size_t addressBytes =
        method >= kFirstLongMethod ? kLongAddressBytes : kShortAddressBytes;
    if (!bits::read_raw(
            reader,
            std::span<std::byte>(candidate.address.data(), addressBytes))) {
        return false;
    }
    candidate.kind = static_cast<std::uint8_t>(kind);
    candidate.address[descriptor::kNetAddrSize - 1] = static_cast<std::byte>(method);
    output = candidate;
    return true;
}

bool read_player_remove(bits::Reader& reader, PlayerRemoveRequest& output) noexcept {
    PlayerRemoveRequest candidate{};
    std::uint64_t playerIndex = 0;
    if (!bits::read_raw_u64(reader, candidate.sessionId)
        || !reader.read(kPlayerIndexWidth, playerIndex)) {
        return false;
    }
    candidate.playerIndex = static_cast<std::uint8_t>(playerIndex);
    output = candidate;
    return true;
}

} // namespace dawn::middleware::gameplay::group
