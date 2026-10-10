#include "../../../../encoding/bit_reader.h"
#include "../../../../encoding/byte_order.h"
#include "internal.h"
#include "activity_manager_reservations.h"
#include "middleware/protobuf/codec.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {
namespace {

using encoding::bits::Reader;

struct SnapshotPrefix final {
    static constexpr std::uint8_t tokenCountWidth = 10;
    static constexpr std::size_t tokenElementWidth = 8;
    static constexpr std::size_t fixedArrayBits = 2048 + 2048 + 8192 + 256;
    static constexpr std::size_t branchScalarBits = 4 + 1 + 8;
    static constexpr std::size_t rootScalarBits = 4 + 32 + 2;
};

// a larger count is a misread; going on would put the descriptor at the wrong offset
constexpr std::uint64_t kTokenCapacity = 568;

} // namespace

bool parse_startup_reservations(std::span<const std::byte> input,
                                StartupReservations& result) noexcept {
    result = {};
    protobuf::Reader fields(input);
    StartupReservations parsed{};
    bool seen = false;
    while (fields.remaining() != 0) {
        protobuf::Field field{};
        if (!fields.next(field)) return false;
        if (field.fieldNumber != 2) continue;
        if (seen || field.wireType != protobuf::WireType::lengthDelimited) return false;
        seen = true;
        if (field.bytes.empty()) continue;
        Reader reader(field.bytes);
        std::uint64_t tokenCount{};
        ActivityManagerSelection descriptor{};
        // exact-build 80808699 fields 0..4; skip sensitive fields without copying them
        if (!reader.read(SnapshotPrefix::tokenCountWidth, tokenCount)
            || tokenCount > kTokenCapacity
            || !reader.skip(static_cast<std::size_t>(tokenCount)*8)
            || !reader.skip(SnapshotPrefix::fixedArrayBits)
            || !reader.skip(SnapshotPrefix::branchScalarBits)
            || !reader.skip(SnapshotPrefix::rootScalarBits)
            || !parse(reader, descriptor)
            // fields 5/6: fixed 256- and 1024-byte arrays; field 7: 808086BB
            || !reader.skip((256+1024)*8)
            || !activity_message::peer_reservation::read_peers(reader, parsed.members)
            // fields 8..20: key64, account64, grace32, ten booleans
            || !reader.skip(64+64+32+10) || reader.remaining_bits()>7) return false;
        parsed.present = true;
    }
    result = parsed;
    return true;
}

bool parse_field_two(std::span<const std::byte> input,
                     ActivityManagerSelection& selection) noexcept {
    Reader reader(input);
    const std::size_t streamBits = input.size() * encoding::kBitsPerByte;
    std::uint64_t tokenCount = 0;
    if (!reader.read(SnapshotPrefix::tokenCountWidth, tokenCount) || tokenCount > kTokenCapacity
        || !reader.skip(static_cast<std::size_t>(tokenCount) * SnapshotPrefix::tokenElementWidth)
        || !reader.skip(SnapshotPrefix::fixedArrayBits)
        || !reader.skip(SnapshotPrefix::branchScalarBits)
        || !reader.skip(SnapshotPrefix::rootScalarBits)) {
        return false;
    }

    ActivityManagerSelection parsed{};
    const std::size_t descriptorStart = streamBits - reader.remaining_bits();
    if (!parse(reader, parsed)) {
        return false;
    }
    (void)copy_bits(input,
                    descriptorStart,
                    streamBits - reader.remaining_bits() - descriptorStart,
                    parsed.descriptorBits,
                    parsed.descriptorBitLength);
    selection = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
