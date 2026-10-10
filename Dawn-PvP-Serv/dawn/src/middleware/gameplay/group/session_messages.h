#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../descriptor/join_descriptor.h"
#include "player_profile.h"

namespace dawn::middleware::gameplay::group {

enum class SessionMessageId : std::uint8_t {
    peerConnect = 11,
    membershipUpdate = 30,
    joinComplete = 12,
    joinAbort = 13,
    leaveSession = 15,
    leaveAcknowledge = 16,
    sessionDisband = 17,
    sessionBoot = 18,
    hostHandoff = 19,
    hostTransition = 21,
    hostReestablish = 22,
    peerReestablish = 25,
    peerEstablish = 26,
    timeSynchronize = 29,
};

inline constexpr std::uint32_t kPeerConnectSize = 24;
inline constexpr std::uint32_t kJoinCompleteSize = 24;
inline constexpr std::uint32_t kJoinAbortSize = 24;
inline constexpr std::uint32_t kLeaveSessionSize = 8;
inline constexpr std::uint32_t kLeaveAcknowledgeSize = 8;
inline constexpr std::uint32_t kSessionDisbandSize = 24;
inline constexpr std::uint32_t kSessionBootSize = 24;
inline constexpr std::uint32_t kHostHandoffSize = 100;
inline constexpr std::uint32_t kHostTransitionSize = 16;
inline constexpr std::uint32_t kHostReestablishSize = 136;
inline constexpr std::uint32_t kPeerReestablishSize = 8;
inline constexpr std::uint32_t kPeerEstablishSize = 8;
inline constexpr std::uint32_t kTimeSynchronizeSize = 48;
inline constexpr std::uint32_t kMembershipUpdateSize = 31104;

inline constexpr std::size_t kMemberCapacity = 32;

struct PeerConnect {
    std::uint16_t protocolVersion{};
    std::uint64_t machineId{};
    std::uint64_t sessionId{};
};

struct JoinComplete {
    std::uint64_t sessionId{};
    std::uint64_t machineId{};
    std::uint32_t joinSequence{};
};

struct SessionNotice {
    std::uint64_t sessionId{};
    std::uint64_t machineId{};
    bool flag{};
};

struct SessionBoot {
    std::uint64_t sessionId{};
    std::uint64_t machineId{};
    std::uint8_t kind{};
    std::uint8_t reason{};
};

// the only native session message that can arm the manager's pending member index; the
// address must byte-match the target member's address in the manager table
struct HostHandoff {
    std::uint64_t sessionId{};
    std::array<std::byte, descriptor::kNetAddrSize> address{};
    std::uint32_t memberIndex{};
};

struct HostTransition {
    std::uint64_t sessionId{};
    std::uint32_t count{};
    std::uint32_t value{};
};

struct HostReestablish {
    std::uint64_t sessionId{};
    std::uint64_t machineId{};
    std::array<std::byte, descriptor::kNetAddrSize> address{};
    std::array<std::byte, 16> identity128{};
    std::array<std::byte, 18> identity144{};
};

static_assert(offsetof(HostReestablish, identity128) == 0x66U);
static_assert(offsetof(HostReestablish, identity144) == 0x76U);
static_assert(sizeof(HostReestablish) == kHostReestablishSize);

struct TimeSynchronize {
    std::uint64_t sessionId{};
    bool threeSample{};
    std::uint64_t sampleA{};
    std::uint64_t sampleB{};
    std::uint64_t sampleC{};
};

[[nodiscard]] bool write_peer_connect(encoding::bits::Writer& writer,
                                      const PeerConnect& body) noexcept;

[[nodiscard]] bool write_join_complete(encoding::bits::Writer& writer,
                                       const JoinComplete& body) noexcept;

[[nodiscard]] bool read_join_complete(encoding::bits::Reader& reader,
                                      JoinComplete& output) noexcept;

[[nodiscard]] bool read_join_abort(encoding::bits::Reader& reader, SessionNotice& output) noexcept;

[[nodiscard]] bool read_session_only(encoding::bits::Reader& reader,
                                     std::uint64_t& output) noexcept;

[[nodiscard]] bool write_session_only(encoding::bits::Writer& writer,
                                      std::uint64_t sessionId) noexcept;

[[nodiscard]] bool write_session_disband(encoding::bits::Writer& writer,
                                         const SessionNotice& body) noexcept;

[[nodiscard]] bool write_session_boot(encoding::bits::Writer& writer,
                                      const SessionBoot& body) noexcept;

[[nodiscard]] bool write_host_handoff(encoding::bits::Writer& writer,
                                      const HostHandoff& body) noexcept;

[[nodiscard]] bool write_host_transition(encoding::bits::Writer& writer,
                                         const HostTransition& body) noexcept;

[[nodiscard]] bool write_host_reestablish(encoding::bits::Writer& writer,
                                          const HostReestablish& body) noexcept;

[[nodiscard]] bool read_host_reestablish(encoding::bits::Reader& reader,
                                         HostReestablish& output) noexcept;

[[nodiscard]] bool read_time_synchronize(encoding::bits::Reader& reader,
                                         TimeSynchronize& output) noexcept;

[[nodiscard]] bool write_time_synchronize(encoding::bits::Writer& writer,
                                          const TimeSynchronize& body) noexcept;

// a ladder; the decoder refuses anything above established. Only values we use are named
enum class MemberState : std::uint8_t {
    reservedAmbassador = 3,
    connected = 5,
    joined = 7,
    waiting = 8,
    ready = 9,
    established = 10,
};

struct MembershipMember {
    std::array<std::byte, descriptor::kNetAddrSize> address{};
    std::uint64_t machineId{};
    // the peer refuses the update when this does not echo the id its own join carries
    std::uint64_t joinId{};
    MemberState state{MemberState::established};
    bool connectionPresent{};
    std::uint32_t joinCompatibility{};
    std::uint64_t joinTimestamp{};
    std::uint8_t connectionValue{};
    bool ownsPlayerSlot{};
    // archived public Citizen sessions predate field 9 and publish field 10 alone; the client
    // renders that peer as pc=0 but keeps its player-table link
    bool ownedPlayerCountPresent{true};
    std::uint32_t playerSlot{};
};

inline constexpr std::size_t kPlayerCapacity = 32;
inline constexpr std::uint8_t kPlayerAdditionNumberWidth = 20;
inline constexpr std::uint32_t kPlayerAdditionNumberMask =
    (std::uint32_t{1} << kPlayerAdditionNumberWidth) - 1U;
[[nodiscard]] constexpr std::uint32_t
advance_player_addition_number(std::uint32_t current) noexcept {
    return (current + 1U) & kPlayerAdditionNumberMask;
}
inline constexpr std::size_t kMembershipDisplayNameCapacity = 64;

struct MembershipPlayer {
    std::uint32_t slot{};
    std::uint64_t playerId{};
    std::uint32_t memberIndex{};
    std::uint32_t addSequence{};
    bool flag{};
    bool profilePresent{};
    std::uint32_t emblemDefinitionIndex{};
    std::int32_t profileType{};
    std::int32_t light{};
    bool displayNamePresent{};
    std::array<char16_t, kMembershipDisplayNameCapacity> displayName{};
    bool identityPresent{};
    std::array<std::byte, 36> identity{};
    PlayerProfileSupplement profileSupplement{};
};

// rejects empty, unterminated, embedded-NUL and non-ASCII names so a bad name can't spill
// into the following 36-byte platform identity field
[[nodiscard]] inline bool set_ascii_membership_display_name(MembershipPlayer& player,
                                                             std::string_view displayName) noexcept {
    player.displayNamePresent = false;
    player.displayName = {};
    if (displayName.empty() || displayName.size() >= player.displayName.size()) {
        return false;
    }
    for (std::size_t index = 0; index < displayName.size(); ++index) {
        const auto value = static_cast<unsigned char>(displayName[index]);
        if (value == 0 || value > 0x7F) {
            player.displayName = {};
            return false;
        }
        player.displayName[index] = static_cast<char16_t>(value);
    }
    player.displayNamePresent = true;
    return true;
}

// PC 1.59 puts the SteamID64 in the first little-endian qword of Codec-A field 1, rest zero;
// only for the lobby owner on the dedicated host row, real endpoint rows keep their own field
[[nodiscard]] inline bool set_synthesized_pc_platform_identity(MembershipPlayer& player,
                                                               std::uint64_t platformId) noexcept {
    player.identityPresent = false;
    player.identity = {};
    if (platformId == 0) {
        return false;
    }
    for (std::size_t byte = 0; byte < sizeof(platformId); ++byte) {
        player.identity[byte] =
            static_cast<std::byte>((platformId >> (byte * 8U)) & std::uint64_t{0xFF});
    }
    player.identityPresent = true;
    return true;
}

struct MembershipUpdate {
    std::uint64_t hostMachineId{};
    // must be at least 1 and strictly increase
    std::uint32_t revision{};
    std::uint32_t hostMemberIndex{};
    std::uint32_t successionIndex{};
    std::span<const MembershipMember> members{};
    std::span<const MembershipPlayer> players{};
    std::uint32_t nextPlayerAdditionNumber{};
    bool playerAdditionNumberPresent{true};
};

// the receiving peer must be in members: it clears its own table and finds itself by NetAddr
[[nodiscard]] bool write_membership_update(encoding::bits::Writer& writer,
                                           const MembershipUpdate& body) noexcept;

} // namespace dawn::middleware::gameplay::group
