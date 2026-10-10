#include "opcode501_codec.h"

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../../encoding/byte_order.h"
#include "../status_fields.h"

namespace dawn::middleware::web_service::messages::opcode501 {
namespace {

constexpr std::uint8_t kCharacterSoidWidth = 64;
constexpr std::int32_t kInt8Bias = 128;
constexpr std::int32_t kInt16Bias = 32768;
// header field 1.11 (accepted): 2 bits, bias of one; the record must carry 1
constexpr std::uint8_t kAcceptedWidth = 2;
constexpr std::size_t kAcceptedOffset = 32;
constexpr std::uint8_t kAccepted = 1;
constexpr std::int32_t kMaximumRace = 2;
constexpr std::int32_t kMaximumGender = 1;
constexpr std::int32_t kMaximumClass = 2;
constexpr std::size_t kNameBits = 64 * 8;

// native offsets of the int16 header fields in wire order: 1.0, 1.4-1.6, 1.7[3], 1.8[3], 1.9
constexpr std::array<std::size_t, 11> kInt16Offsets{0, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};
// int8 fields 1.1-1.3 sit between 1.0 and 1.4 on the wire
constexpr std::array<std::size_t, 3> kInt8Offsets{2, 3, 4};
constexpr std::size_t kHashOffset = 28;

[[nodiscard]] bool read_int16(encoding::bits::Reader& reader,
                              std::array<std::uint8_t, kIdentityHeaderSize>& header,
                              std::size_t offset) noexcept {
    std::uint64_t wire = 0;
    if (!reader.read(16, wire)) {
        return false;
    }
    const auto value =
        static_cast<std::uint16_t>(static_cast<std::int32_t>(wire) - kInt16Bias);
    header[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    header[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
    return true;
}

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t wire = 0;
    std::array<std::int32_t, 3> identity{};
    // field 0: three int8, each behind its own presence bit (interleaved, not grouped)
    for (std::int32_t& value : identity) {
        if (!reader.read(1, wire) || wire == 0 || !reader.read(8, wire)) {
            return false;
        }
        value = static_cast<std::int32_t>(wire) - kInt8Bias;
    }
    if (identity[0] < 0 || identity[0] > kMaximumRace || identity[1] < 0
        || identity[1] > kMaximumGender || identity[2] < 0 || identity[2] > kMaximumClass) {
        return false;
    }
    request.race = static_cast<std::uint8_t>(identity[0]);
    request.gender = static_cast<std::uint8_t>(identity[1]);
    request.characterClass = static_cast<std::uint8_t>(identity[2]);

    auto& header = request.identityHeader;
    if (!read_int16(reader, header, kInt16Offsets[0])) {
        return false;
    }
    for (const std::size_t offset : kInt8Offsets) {
        if (!reader.read(8, wire)) {
            return false;
        }
        header[offset] = static_cast<std::uint8_t>(static_cast<std::int32_t>(wire) - kInt8Bias);
    }
    for (std::size_t index = 1; index < kInt16Offsets.size(); ++index) {
        if (!read_int16(reader, header, kInt16Offsets[index])) {
            return false;
        }
    }
    if (!reader.read(32, wire)) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        header[kHashOffset + index] = static_cast<std::uint8_t>((wire >> (index * 8U)) & 0xFFU);
    }
    // client sends 0 for accepted; read past it and store 1 so the record says accepted
    if (!reader.read(kAcceptedWidth, wire)) {
        return false;
    }
    header[kAcceptedOffset] = kAccepted;
    if (reader.remaining_bits() >= kNameBits) {
        (void)reader.skip(kNameBits);
    }
    return true;
}

bool encode_response(const Message& message,
                     std::int32_t nextVersion,
                     std::uint64_t characterSoid,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kEnvelopeHeaderSize) {
        return false;
    }
    encoding::write_u16_be(output.first<encoding::kU16Size>(), message.opcode);
    encoding::write_u32_be(output.subspan<encoding::kU16Size, encoding::kU32Size>(),
                           message.transactionId);

    encoding::bits::Writer writer(output.subspan(kEnvelopeHeaderSize));
    // client reads dword +4 of the reply as the family-4 version to await, same as opcode 505
    StatusResponse status{};
    status.value = nextVersion;
    bool encoded = status::write_fields(writer, ResponseShape::statusPair, status)
                   && writer.write(characterSoid, kCharacterSoidWidth)
                   && writer.write(0, kAbsentTrailerWidth);
    std::size_t payloadSize = 0;
    if (!encoded || !writer.finish(payloadSize)) {
        return false;
    }
    written = kEnvelopeHeaderSize + payloadSize;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode501
