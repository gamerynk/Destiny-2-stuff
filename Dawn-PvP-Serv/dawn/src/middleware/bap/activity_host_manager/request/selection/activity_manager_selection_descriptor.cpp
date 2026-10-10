#include "../../../../encoding/bit_reader.h"
#include "../../../../encoding/byte_order.h"
#include "internal.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {
namespace {

using encoding::bits::Reader;

struct FieldOneLayout final {
    static constexpr std::size_t opcode = 0;
    static constexpr std::size_t transaction = opcode + encoding::kU16Size;
    static constexpr std::size_t bitstream = transaction + encoding::kU32Size;
};

constexpr std::uint16_t kActivityManagerOpcode = 1'303;
constexpr std::uint8_t kRequestKindWidth = 2;
constexpr std::int8_t kRequestKindBias = 1;
constexpr std::uint8_t kTrailingFlagWidth = 1;

} // namespace

bool parse_field_one(std::span<const std::byte> input,
                     ActivityManagerSelection& selection) noexcept {
    if (input.size() < FieldOneLayout::bitstream
        || encoding::read_u16_be(input.subspan<FieldOneLayout::opcode, encoding::kU16Size>())
               != kActivityManagerOpcode) {
        return false;
    }

    const std::span<const std::byte> stream = input.subspan(FieldOneLayout::bitstream);
    Reader reader(stream);
    const std::size_t streamBits = stream.size() * encoding::kBitsPerByte;
    std::uint64_t encodedKind = 0;
    ActivityManagerSelection parsed{};
    if (!reader.read(kRequestKindWidth, encodedKind)) {
        return false;
    }
    parsed.requestKind =
        static_cast<std::int8_t>(static_cast<std::int64_t>(encodedKind) - kRequestKindBias);
    const std::size_t descriptorStart = streamBits - reader.remaining_bits();
    if (!parse(reader, parsed)) {
        return false;
    }
    // the message that echoes the descriptor replays it as-is, so keep its exact bits; if
    // too big for storage it still parses and only the replay re-encodes the scalars
    (void)copy_bits(stream,
                    descriptorStart,
                    streamBits - reader.remaining_bits() - descriptorStart,
                    parsed.descriptorBits,
                    parsed.descriptorBitLength);

    std::uint64_t trailingFlag = 0;
    if (!reader.read(kTrailingFlagWidth, trailingFlag)) {
        return false;
    }
    parsed.trailingFlag = trailingFlag != 0;
    selection = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
