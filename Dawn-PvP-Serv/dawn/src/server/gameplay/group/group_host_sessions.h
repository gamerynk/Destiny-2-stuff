#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::state::activity { struct SessionRecord; }

namespace dawn::server::gameplay::group {

inline constexpr std::int32_t kUnknownRegion = -1;

struct HostSessionRow {
    std::uint64_t groupSessionId{};
    std::uint64_t hostSessionId{};
    std::int32_t regionIndex{};
};

[[nodiscard]] std::uint64_t held_host_session(std::uint64_t groupSessionId) noexcept;

// central fireteam sessions have no row and return the absent id, keeping their lobby-authenticated
// path separate from the activity-authenticated region path
[[nodiscard]] std::uint64_t source_activity_session(std::uint64_t groupSessionId) noexcept;

[[nodiscard]] bool source_activity_snapshot(std::uint64_t groupSessionId,
                                            state::activity::SessionRecord& output) noexcept;

[[nodiscard]] std::uint64_t
source_activity_session_for_host(std::uint64_t hostSessionId) noexcept;

[[nodiscard]] std::int32_t region_for_host_session(std::uint64_t hostSessionId) noexcept;

// native host migration changes the group id but not the activity instance; the old id stays valid
// for the reliable link while the new one keys session messages, so both resolve to the same host
[[nodiscard]] std::uint64_t alias_host_session(std::uint64_t sourceGroupSessionId,
                                               std::uint64_t migratedGroupSessionId) noexcept;

void snapshot_host_sessions(std::span<HostSessionRow> output, std::size_t& count) noexcept;

// the allocation advances the state revision, so it must never run inside a staged push or that
// push fails its own revision guard. Callers hold no lock
void allocate_claimed_host_sessions() noexcept;

void reset_host_sessions() noexcept;

} // namespace dawn::server::gameplay::group
