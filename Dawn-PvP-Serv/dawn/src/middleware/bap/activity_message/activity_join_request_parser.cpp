#include "activity_join_request_parser.h"
#include "client_authoritative_data.h"

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message::join_request {
namespace {

struct JoinRequestLayout final {
    static constexpr std::size_t correlation = 0;
    static constexpr std::size_t sessionId = correlation + encoding::kU32Size;
    static constexpr std::size_t memberKey = sessionId + encoding::kU64Size;
    static constexpr std::size_t prefixSize = memberKey + encoding::kU64Size;
};

constexpr std::size_t kCharacterSoidBit = 330;
constexpr std::uint8_t kCharacterSoidWidth = 64;

// a short payload is normal; the roster falls back to the selected character when
// this is missing
[[nodiscard]] std::uint64_t read_character_soid(std::span<const std::byte> input) noexcept {
    encoding::bits::Reader reader(input);
    std::uint64_t value = 0;
    if (!reader.skip(kCharacterSoidBit) || !reader.read(kCharacterSoidWidth, value)) {
        return 0;
    }
    return value;
}

} // namespace

bool parse_join_request(std::span<const std::byte> input, JoinRequest& request) noexcept {
    request = {};
    if (input.size() < JoinRequestLayout::prefixSize) {
        return false;
    }

    JoinRequest parsed{};
    parsed.correlation =
        encoding::read_u32_be(input.subspan<JoinRequestLayout::correlation, encoding::kU32Size>());
    parsed.sessionId =
        encoding::read_u64_be(input.subspan<JoinRequestLayout::sessionId, encoding::kU64Size>());
    parsed.memberKey =
        encoding::read_u64_le(input.subspan<JoinRequestLayout::memberKey, encoding::kU64Size>());
    if (parsed.sessionId == kAbsentSessionId) {
        return false;
    }
    parsed.characterSoid = read_character_soid(input);

    // B2 follows correlation32 + session64 + identity362. Addresses are often sent only
    // here; never keep a partially decoded B2 record
    encoding::bits::Reader detailReader(input);
    core::protocol::ActivityPeerConnection connection{};
    if(detailReader.skip(458)
        && client_authoritative_data::read_peer_connection_branch(detailReader,connection))
        parsed.peerConnection=connection;

    request = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::join_request
