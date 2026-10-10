#include "join_messages.h"

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kProtocolWidth = 16;
constexpr std::uint8_t kBuildWidth = 32;
constexpr std::uint8_t kExecutableWidth = 3;
constexpr std::uint8_t kReasonWidth = 6;
} // namespace

bool read_join_request(bits::Reader& reader, JoinRequest& output) noexcept {
    std::uint64_t protocol = 0;
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    std::uint64_t executable = 0;
    JoinRequest candidate{};
    if (!reader.read(kProtocolWidth, protocol) || !reader.read(kBuildWidth, minimum)
        || !reader.read(kBuildWidth, maximum) || !reader.read(kExecutableWidth, executable)
        || !bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.joinId)) {
        return false;
    }
    candidate.protocolVersion = static_cast<std::uint16_t>(protocol);
    candidate.minimumBuild = static_cast<std::uint32_t>(minimum);
    candidate.maximumBuild = static_cast<std::uint32_t>(maximum);
    candidate.executableType = static_cast<std::uint8_t>(executable);
    output = candidate;
    return true;
}

bool read_join_members(bits::Reader& reader,JoinMembers& output) noexcept {
    JoinMembers candidate{};
    std::uint64_t count{};
    if (!reader.read(5,count)) return false;
    candidate.peerCount=static_cast<std::size_t>(count);
    for (std::size_t i=0;i<candidate.peerCount;++i) {
        auto& peer=candidate.peers[i];
        std::uint64_t method{},field{},present{},index{};
        if (!reader.read(3,method)) return false;
        const std::size_t addressSize=method>=6?85:41;
        if (!bits::read_raw(reader,std::span(peer.address).first(addressSize))
            || !bits::read_raw_u64(reader,peer.opaquePeerId)
            || !reader.read(16,field) || !reader.read(1,present)
            || (present && !reader.read(5,index))) return false;
        peer.address.back()=static_cast<std::byte>(method);
        peer.opaquePeerField=static_cast<std::uint16_t>(field);
        peer.playerIndex=present?static_cast<std::int32_t>(index):-1;
    }
    if (!reader.read(5,count)) return false;
    candidate.playerCount=static_cast<std::size_t>(count);
    for (std::size_t i=0;i<candidate.playerCount;++i) {
        auto& player=candidate.players[i];
        std::uint64_t flags{};
        // RVA A496E0 reads exactly 288 raw bits; its 48-byte native storage is padded
        if (!bits::read_raw_u64(reader,player.playerId)
            || !bits::read_raw(reader,player.identity) || !reader.read(4,flags)) return false;
        player.flags=static_cast<std::uint8_t>(flags);
    }
    for (std::size_t i=0;i<candidate.peerCount;++i)
        if (candidate.peers[i].playerIndex>=static_cast<std::int32_t>(candidate.playerCount)) return false;
    output=candidate;
    return true;
}

bool write_join_refuse(bits::Writer& writer, const JoinRefuse& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId) && bits::write_raw_u64(writer, body.joinId)
           && writer.write(static_cast<std::uint64_t>(body.reason), kReasonWidth);
}

bool answerable(const JoinRequest& request) noexcept {
    return request.protocolVersion == kProtocolVersion;
}

bool admit(const JoinRequest& request, std::uint64_t hostSessionId, RefuseReason& reason) noexcept {
    if (!answerable(request)) {
        return false;
    }
    (void)hostSessionId;
    if (request.maximumBuild < kHostBuild) {
        reason = RefuseReason::peerVersionTooLow;
        return false;
    }
    if (request.minimumBuild > kHostBuild) {
        reason = RefuseReason::hostVersionTooLow;
        return false;
    }
    if (request.executableType != kExecutableType) {
        reason = RefuseReason::executableTypeMismatch;
        return false;
    }
    return true;
}

} // namespace dawn::middleware::gameplay::peer
