#pragma once

#include <cstdint>

namespace dawn::state::activity::mission_host {

// one slot, keyed by the activity session all members share. A second fireteam launching
// the same mission is refused: every mission controller is a single static instance

using RegionRank = int (*)(int region) noexcept;

[[nodiscard]] std::uint64_t bind(std::uint64_t sessionId, std::uint64_t now) noexcept;

[[nodiscard]] std::uint64_t run() noexcept;

[[nodiscard]] std::uint64_t session() noexcept;

void note_arrived(std::uint64_t sessionId) noexcept;

[[nodiscard]] bool armed() noexcept;

void note_region(std::uint64_t sessionId, int region, RegionRank rank) noexcept;

[[nodiscard]] int region() noexcept;

void note_member(std::uint64_t sessionId, std::uint64_t platformId) noexcept;

[[nodiscard]] bool member(std::uint64_t platformId) noexcept;

// only the peer holding the bubble grant simulates that AI, so only it sees authentic enemy state
void note_authority(std::uint64_t sessionId, std::uint64_t platformId) noexcept;

[[nodiscard]] bool authoritative(std::uint64_t platformId) noexcept;

void release(std::uint64_t sessionId) noexcept;

} // namespace dawn::state::activity::mission_host
