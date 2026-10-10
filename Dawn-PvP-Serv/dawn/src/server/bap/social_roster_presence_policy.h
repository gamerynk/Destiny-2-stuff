#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace dawn::server::bap::social_roster_presence {

// keep the known-good orbit entry, not the native absent-index sentinel
inline constexpr std::int16_t kOrbitActivity = 0;
inline constexpr std::int16_t kMaxActivity = 4094;
inline constexpr std::uint64_t kPollIntervalMs = 1000;

[[nodiscard]] constexpr bool eligible(std::uint64_t requestedAccount,
                                       std::uint64_t connectionAccount,
                                       bool authenticated,
                                       bool open,
                                       bool joined,
                                       bool foreign) noexcept {
    return requestedAccount != 0 && requestedAccount == connectionAccount
           && authenticated && open && joined && !foreign;
}

struct ActivityChoice final {
    std::uint64_t newestOpenedTick{};
    std::int16_t index{kOrbitActivity};
    bool found{};

    void consider(std::uint64_t openedTick, std::int16_t activityIndex) noexcept {
        if (activityIndex < 0 || activityIndex > kMaxActivity
            || (found && openedTick < newestOpenedTick)) {
            return;
        }
        newestOpenedTick = openedTick;
        index = activityIndex;
        found = true;
    }
};

struct ArrivalLatch final {
    std::uint64_t sessionId{};
    bool arrived{};

    void note_delivered(std::uint64_t activitySession,
                        bool privateOwner,
                        bool clientSyncKnown,
                        bool awaitingClientSync) noexcept {
        if (activitySession != 0 && privateOwner && clientSyncKnown && !awaitingClientSync) {
            sessionId = activitySession;
            arrived = true;
        }
    }

    [[nodiscard]] bool ready(std::uint64_t activitySession) const noexcept {
        return arrived && activitySession != 0 && activitySession == sessionId;
    }
};

[[nodiscard]] constexpr std::uint64_t next_poll(std::uint64_t now) noexcept {
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    return now > maximum - kPollIntervalMs ? maximum : now + kPollIntervalMs;
}

struct Observation final {
    std::uint64_t root{};
    std::uint64_t nextPollTick{};
    std::int16_t publishedActivity{};

    [[nodiscard]] bool changed(std::uint64_t now, std::int16_t current) noexcept {
        if (root == 0 || now < nextPollTick) return false;
        nextPollTick = next_poll(now);
        return current != publishedActivity;
    }
};

struct Tracker final {
    std::array<Observation, 16> observations{};

    [[nodiscard]] bool subscribe(std::uint64_t root,
                                 std::int16_t activity,
                                 std::uint64_t now) noexcept {
        if (root == 0) return false;
        Observation* slot = nullptr;
        for (auto& observation : observations) {
            if (observation.root == root) { slot = &observation; break; }
            if (observation.root == 0 && slot == nullptr) slot = &observation;
        }
        if (slot == nullptr) return false;
        *slot = {root, next_poll(now), activity};
        return true;
    }

    void unsubscribe(std::uint64_t root) noexcept {
        for (auto& observation : observations) {
            if (observation.root == root) observation = {};
        }
    }
};

} // namespace dawn::server::bap::social_roster_presence
