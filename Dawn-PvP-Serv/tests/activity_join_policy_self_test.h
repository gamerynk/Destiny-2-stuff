#pragma once

#include <array>
#include <cstdio>

#include "middleware/bap/activity_message/sensor_auth_update.h"
#include "server/bap/encrypted/push/activity/activity_join_policy.h"

namespace restoration::tests {

inline bool activity_join_policy_self_test() noexcept {
    namespace wire = dawn::middleware::bap::activity_message::sensor_auth_update;
    using dawn::server::bap::encrypted::push::activity::activity_allows_join_in_progress;
    const auto check = [](bool passed, const char* step) noexcept {
        if (!passed) std::printf("FAIL: activity join policy: %s\n", step);
        return passed;
    };
    unsigned checks = 0;
    for (bool reported : {false, true}) {
        for (int region : {-1, 0, 1, 40, 120, 511}) {
            for (std::uint64_t revision : {0ULL, 1ULL, 2ULL, 1000ULL}) {
                for (bool held : {false, true}) {
                    const bool expected = reported && region >= 0 && revision != 0 && !held;
                    if (!check(activity_allows_join_in_progress(reported, region, revision, held)
                               == expected, "unknown/held/negative/ready policy")) return false;
                    ++checks;
                }
            }
        }
    }
    // native schema 0x8080991A: byte0 and byte1 are biased enums, byte2 an unconditional bool,
    // so allow-joining is bit 7, not a presence bit
    for (const auto lifetime : wire::kLifetimeStates) {
        for (bool spawnOverride : {false, true}) {
            wire::Snapshot state{};
            state.lifetime = lifetime;
            state.hasSpawnOverride = spawnOverride;
            state.spawnSliceSet = 40;
            state.spawnSetHash = 0x12345678;
            std::array<std::byte, 65> closed{}, opened{}, closedAgain{};
            const auto encode = [&](bool allowed, auto& out) noexcept {
                state.allowJoinInProgress = allowed;
                dawn::middleware::encoding::bits::Writer writer(out);
                std::size_t written = 0;
                return wire::write_auth_body(writer, state, 0, 17, 0, false)
                    && writer.bit_count() == 520 && writer.finish(written) && written == 65;
            };
            if (!check(encode(false, closed) && encode(true, opened)
                       && encode(false, closedAgain), "exact existing 520-bit lifetime body")) return false;
            if (!check((std::to_integer<unsigned>(opened[0]) & 1U) == 1U,
                       "ready activity emits native join permission")) return false;
            if (!check(closed == closedAgain && (std::to_integer<unsigned>(closed[0]) & 1U) == 0,
                       "permission closes again without retaining stale state")) return false;
            auto expected = closed;
            expected[0] |= std::byte{1};
            if (!check(opened == expected, "only native bool changes; remaining 519 bits preserved")) return false;
            const unsigned decodedLifetime = (std::to_integer<unsigned>(opened[0]) >> 4U) - 1U;
            if (!check(decodedLifetime == lifetime, "lifetime and spawn override preserved")) return false;
            checks += 5;
        }
    }
    std::printf("PASS: activity join policy and native lifetime wire (%u checks)\n", checks);
    return true;
}

} // namespace restoration::tests
