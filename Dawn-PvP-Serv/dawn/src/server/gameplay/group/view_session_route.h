#pragma once

#include <cstdint>
#include <span>

namespace dawn::server::gameplay::group::view_session_route {

struct Candidate final {
    std::uint64_t sessionId{};
    std::uint64_t lastUse{};
    bool occupied{};
    bool endpointMatches{};
};

// during a public region handoff a link briefly carries both the old and new target sessions, so
// transport supplies zero; view bodies name no session, so route to the most recently admitted target
[[nodiscard]] inline std::uint64_t resolve(
    std::uint64_t transportSessionId,
    std::span<const Candidate> candidates) noexcept {
    if (transportSessionId != 0) {
        return transportSessionId;
    }

    std::uint64_t selectedSessionId = 0;
    std::uint64_t newestUse = 0;
    for (const Candidate& candidate : candidates) {
        if (!candidate.occupied || !candidate.endpointMatches || candidate.sessionId == 0) {
            continue;
        }
        if (selectedSessionId == 0 || candidate.lastUse > newestUse) {
            selectedSessionId = candidate.sessionId;
            newestUse = candidate.lastUse;
        }
    }
    return selectedSessionId;
}

} // namespace dawn::server::gameplay::group::view_session_route
