#include "activity_client_identity_parser.h"

#include <bit>

#include "../../encoding/bit_reader.h"

namespace dawn::middleware::bap::activity_message::client_identity {
namespace {

constexpr std::uint32_t kField1Bias = 1;
constexpr std::uint32_t kField2Bias = 0x80000000U;
constexpr std::size_t kMemberKeyByteCount = 8;
constexpr std::uint8_t kPaddingBitCount = 6;

[[nodiscard]] bool read_member_key(encoding::bits::Reader& reader,
                                   std::uint64_t& memberKey) noexcept {
    memberKey = 0;
    for (std::size_t index = 0; index < kMemberKeyByteCount; ++index) {
        std::uint64_t byte = 0;
        if (!reader.read(8, byte)) {
            return false;
        }
        memberKey |= byte << (index * 8U);
    }
    return true;
}

} // namespace

bool parse_client_identity(std::span<const std::byte> input, ClientIdentity& identity) noexcept {
    identity = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    encoding::bits::Reader reader(input);
    ClientIdentity parsed{};
    std::uint64_t field1Wire = 0;
    std::uint64_t field2Wire = 0;
    std::uint64_t padding = 0;
    if (!read_member_key(reader, parsed.memberKey) || !reader.read(10, field1Wire)
        || !reader.read(32, field2Wire) || !reader.read(64, parsed.field3)
        || !reader.read(64, parsed.accountSoid) || !reader.read(64, parsed.field5)
        || !reader.read(64, parsed.field6) || !reader.read(kPaddingBitCount, padding)) {
        return false;
    }
    (void)padding;

    const auto field1Stored = static_cast<std::uint32_t>(field1Wire) - kField1Bias;
    const auto field2Stored = static_cast<std::uint32_t>(field2Wire) - kField2Bias;
    parsed.field1 = std::bit_cast<std::int32_t>(field1Stored);
    parsed.field2 = std::bit_cast<std::int32_t>(field2Stored);
    identity = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::client_identity
