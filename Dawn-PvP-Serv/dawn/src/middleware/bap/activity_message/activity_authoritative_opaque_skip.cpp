#include "client_authoritative_data.h"
#include "activity_player_team_parser.h"

namespace dawn::middleware::bap::activity_message::client_authoritative_data {
namespace {

constexpr std::uint8_t kLargeArrayCountWidth = 14;
constexpr std::uint64_t kLargeArrayMaximumCount = 10'000;
constexpr std::uint8_t kSmallArrayCountWidth = 8;
constexpr std::uint64_t kSmallArrayMaximumCount = 128;
constexpr std::size_t kArrayElementBitCount = 8;

[[nodiscard]] bool skip_optional(encoding::bits::Reader& reader, std::size_t width) noexcept {
    bool present = false;
    return read_presence(reader, present) && (!present || reader.skip(width));
}

[[nodiscard]] bool skip_d9(encoding::bits::Reader& reader) noexcept {
    std::uint64_t count = 0;
    return reader.skip(8) && reader.skip(2) && reader.read(kSmallArrayCountWidth, count)
           && count <= kSmallArrayMaximumCount
           && reader.skip(static_cast<std::size_t>(count) * kArrayElementBitCount)
           && reader.skip(64);
}

constexpr std::uint8_t kRegionIndexWidth = 10;
constexpr std::uint8_t kRegionHashWidth = 32;
constexpr std::int32_t kSignedFieldBias = 1;

// both legs have the same shape and only the second names the player's region, so the first
// is walked and nothing is kept
[[nodiscard]] bool
read_d6(encoding::bits::Reader& reader, RegionState& region, bool keepRegion) noexcept {
    std::uint64_t indexWire = 0;
    std::uint64_t hash = 0;
    if (!reader.read(kRegionIndexWidth, indexWire) || !reader.read(kRegionHashWidth, hash)
        || !reader.skip(32) || !reader.skip(2) || !reader.skip(2) || !skip_optional(reader, 8)) {
        return false;
    }
    if (keepRegion) {
        region.index = static_cast<std::int32_t>(indexWire) - kSignedFieldBias;
        region.hash = static_cast<std::uint32_t>(hash);
        region.hasHash = true;
    }
    bool d9Present = false;
    return read_presence(reader, d9Present) && (!d9Present || skip_d9(reader));
}

[[nodiscard]] bool read_player_array(encoding::bits::Reader& reader,
    core::protocol::ActivityPlayerTeam& team) noexcept {
    bool present = false;
    std::uint64_t count = 0;
    if (!read_presence(reader, present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    if (!reader.read(kLargeArrayCountWidth,count) || count>kLargeArrayMaximumCount
        || reader.remaining_bits()<count*kArrayElementBitCount) return false;
    std::array<std::byte,kLargeArrayMaximumCount> blob{};
    for (std::size_t i=0;i<count;++i) {
        std::uint64_t value{};
        if (!reader.read(8,value)) return false;
        blob[i]=std::byte{static_cast<unsigned char>(value)};
    }
    static_cast<void>(player_team::decode(std::span(blob).first(static_cast<std::size_t>(count)),team));
    return true;
}

} // namespace

bool read_presence(encoding::bits::Reader& reader, bool& present) noexcept {
    present = false;
    std::uint64_t wire = 0;
    if (!reader.read(1, wire)) {
        return false;
    }
    present = wire != 0;
    return true;
}

bool skip_opaque_root_branch(encoding::bits::Reader& reader) noexcept {
    core::protocol::ActivityPeerConnection ignored{};
    return read_peer_connection_branch(reader,ignored);
}

bool read_peer_connection_branch(encoding::bits::Reader& reader,
    core::protocol::ActivityPeerConnection& output) noexcept {
    output={};
    core::protocol::ActivityPeerConnection parsed{};
    std::uint64_t flags{};
    if(!read_presence(reader,parsed.hasFlags)
        || (parsed.hasFlags && !reader.read(6,flags))) return false;
    parsed.flags=static_cast<std::uint8_t>(flags);
    if(!(skip_optional(reader,128U*8U) && skip_optional(reader,64)
        && skip_optional(reader,64) && skip_optional(reader,64) && skip_optional(reader,64)
        && skip_optional(reader,32) && skip_optional(reader,32)
        && skip_optional(reader,64) && skip_optional(reader,32))) return false;
    const auto readAddress=[&](auto& address,bool& present) noexcept {
        if(!read_presence(reader,present)) return false;
        if(present) for(auto& element:address){
            std::uint64_t value{};if(!reader.read(8,value)) return false;
            element=std::byte{static_cast<unsigned char>(value)};
        }
        return true;
    };
    if(!readAddress(parsed.proxy,parsed.hasProxy) || !readAddress(parsed.peer,parsed.hasPeer)
        || !readAddress(parsed.alternate,parsed.hasAlternate)
        || !skip_optional(reader,64U*8U) || !read_player_array(reader,parsed.playerTeam)
        || !reader.skip(1)) return false;
    output=parsed;
    return true;
}

constexpr std::size_t kRegionLegIndex = 1;

bool read_transition_branch(encoding::bits::Reader& reader,
                            ClientAuthoritativeData& update) noexcept {
    for (std::size_t leg = 0; leg < 2; ++leg) {
        bool present = false;
        if (!read_presence(reader, present)) {
            return false;
        }
        if (!present) {
            continue;
        }
        RegionState region{};
        const bool keepRegion = leg == kRegionLegIndex;
        if (!read_d6(reader, region, keepRegion)) {
            return false;
        }
        if (keepRegion) {
            update.region = region;
            update.hasRegion = true;
        }
    }

    bool tokenPresent = false;
    std::uint64_t token = 0;
    if (!read_presence(reader, tokenPresent) || (tokenPresent && !reader.read(8, token))) {
        return false;
    }
    if (tokenPresent) {
        update.transitionToken = static_cast<std::uint8_t>(token);
        update.hasTransitionToken = true;
    }
    return skip_optional(reader, 8) && skip_optional(reader, 3);
}

} // namespace dawn::middleware::bap::activity_message::client_authoritative_data
