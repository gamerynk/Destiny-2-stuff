#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../middleware/encoding/bit_reader.h"
#include "../../../middleware/gameplay/peer/join_messages.h"
#include "../../../state/gameplay/definition.h"

namespace dawn::server::gameplay::group {

inline constexpr std::size_t kUnknownBodyBitCount = static_cast<std::size_t>(-1);

[[nodiscard]] bool consume(const state::gameplay::Endpoint& from,
                           std::uint64_t sessionId,
                           std::uint8_t id,
                           middleware::encoding::bits::Reader& reader,
                           std::uint64_t now,
                           std::size_t exactBodyBitCount = kUnknownBodyBitCount) noexcept;

// names the host and the peer, since the peer looks itself up by address; it accepts this only while
// the state replica behind its hash is byte exact
[[nodiscard]] bool publish_membership(const state::gameplay::Endpoint& peer,
                                      std::uint64_t peerJoinId,
                                      std::uint64_t sessionId,
                                      const middleware::gameplay::peer::JoinMembers* nativeMembers=nullptr) noexcept;

// one id per region, because the peer routes every activity push by it alone; it must not change
// while the region keeps its slot or the peer errors public_activity_host_mismatch
[[nodiscard]] std::uint64_t activity_host_session(std::uint64_t groupSessionId,
                                                  std::int32_t regionIndex) noexcept;

[[nodiscard]] std::uint64_t activity_host_session(std::uint64_t groupSessionId,
                                                  std::int32_t regionIndex,
                                                  std::uint64_t sourceActivitySessionId,
                                                  bool& claimedSlot) noexcept;

struct AdmittedRow {
    std::uint64_t sessionId{};
    state::gameplay::Endpoint endpoint{};
    bool joinComplete{};
    bool activityHostPublished{};
    bool playerPublished{};
};

void snapshot_admitted(std::span<AdmittedRow> output, std::size_t& count) noexcept;

struct NativeActivityBinding {
    std::uint64_t account{}, sourceActivity{}, member{}, createdRevision{}, joinedRevision{}, joinId{};
};
[[nodiscard]] bool native_activity_binding(const state::gameplay::Endpoint& endpoint,
    std::uint64_t groupSessionId, NativeActivityBinding& output, bool includePublishedJoining = false) noexcept;

void service(std::uint64_t now) noexcept;

// the peer's own tick refuses to complete until it has applied one update, whatever the update names
[[nodiscard]] bool publish_join_parameters(const state::gameplay::Endpoint& peer,
                                           std::uint64_t sessionId) noexcept;

[[nodiscard]] bool view_accepted(const state::gameplay::Endpoint& peer,
                                 std::uint64_t sessionId) noexcept;

[[nodiscard]] bool authorizes_player_publication(const state::gameplay::Endpoint& peer,
                                                 std::uint64_t sessionId) noexcept;

[[nodiscard]] bool ready_for_player_properties_replay(const state::gameplay::Endpoint& peer,
                                                      std::uint64_t sessionId) noexcept;

void release_endpoint(const state::gameplay::Endpoint& endpoint) noexcept;

void reset() noexcept;

} // namespace dawn::server::gameplay::group
