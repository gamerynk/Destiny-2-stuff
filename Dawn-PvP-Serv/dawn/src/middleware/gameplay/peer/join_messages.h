#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../descriptor/join_descriptor.h"

namespace dawn::middleware::gameplay::peer {

enum class JoinId : std::uint8_t {
    request = 10,
    refuse = 14,
};

inline constexpr std::uint32_t kJoinRequestSize = 6144;
inline constexpr std::uint32_t kJoinRefuseSize = 24;

inline constexpr std::uint16_t kProtocolVersion = 0xA4F8;
inline constexpr std::uint32_t kHostBuild = 86657;
inline constexpr std::uint8_t kExecutableType = 5;

enum class RefuseReason : std::uint8_t {
    notFound = 4,
    peerVersionTooLow = 27,
    hostVersionTooLow = 28,
    executableTypeMismatch = 29,
};

struct JoinRequest {
    std::uint16_t protocolVersion{};
    std::uint32_t minimumBuild{};
    std::uint32_t maximumBuild{};
    std::uint8_t executableType{};
    std::uint64_t sessionId{};
    // identifies this join attempt, not the machine; changes on every retry and the peer
    // refuses a membership update that does not echo it
    std::uint64_t joinId{};
};

// ID10's native reader at RVA 16DD260; one optional local-player index per peer (build 86657).
// Identity claims, not authentication; match against the live PRIVATE session
inline constexpr std::size_t kJoinTableCapacity=31;
struct JoinPeer {
    std::array<std::byte,descriptor::kNetAddrSize> address{};
    std::uint64_t opaquePeerId{};
    std::uint16_t opaquePeerField{};
    std::int32_t playerIndex{-1};
};
struct JoinPlayer {
    std::uint64_t playerId{};
    std::array<std::byte,36> identity{};
    std::uint8_t flags{};
};
struct JoinMembers {
    std::array<JoinPeer,kJoinTableCapacity> peers{};
    std::array<JoinPlayer,kJoinTableCapacity> players{};
    std::size_t peerCount{};
    std::size_t playerCount{};
};
[[nodiscard]] bool read_join_members(encoding::bits::Reader& reader,JoinMembers& output) noexcept;

struct JoinRefuse {
    std::uint64_t sessionId{};
    std::uint64_t joinId{};
    RefuseReason reason{RefuseReason::notFound};
};

[[nodiscard]] bool read_join_request(encoding::bits::Reader& reader, JoinRequest& output) noexcept;

[[nodiscard]] bool write_join_refuse(encoding::bits::Writer& writer,
                                     const JoinRefuse& body) noexcept;

[[nodiscard]] bool
admit(const JoinRequest& request, std::uint64_t hostSessionId, RefuseReason& reason) noexcept;

[[nodiscard]] bool answerable(const JoinRequest& request) noexcept;

} // namespace dawn::middleware::gameplay::peer
