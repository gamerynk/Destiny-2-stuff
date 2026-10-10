#include "activity_spawn_sync.h"

#include <algorithm>
#include <array>
#include <mutex>

namespace dawn::server::bap::encrypted::push::activity::spawn_sync {
namespace {

constexpr std::size_t kEntryCapacity = 64;

struct Entry {
    std::uint64_t platformId{};
    std::uint64_t transitionUntilTick{};
    std::uint64_t lastTouchedTick{};
    std::uint64_t revision{};
    bool awaitingClientSync{};
    std::uint64_t joinSessionId{};
};

std::array<Entry, kEntryCapacity> g_entries{};
std::mutex g_lock;

[[nodiscard]] Entry* find(std::uint64_t platformId) noexcept {
    const auto found = std::find_if(g_entries.begin(), g_entries.end(), [platformId](const Entry& entry) {
        return entry.platformId == platformId;
    });
    return found == g_entries.end() ? nullptr : &*found;
}

[[nodiscard]] Entry& find_or_allocate(std::uint64_t platformId, std::uint64_t nowTick) noexcept {
    if (Entry* existing = find(platformId); existing != nullptr) {
        return *existing;
    }
    auto selected = std::find_if(g_entries.begin(), g_entries.end(), [](const Entry& entry) {
        return entry.platformId == 0;
    });
    if (selected == g_entries.end()) {
        selected = std::min_element(
            g_entries.begin(), g_entries.end(), [](const Entry& left, const Entry& right) {
                return left.lastTouchedTick < right.lastTouchedTick;
            });
    }
    *selected = {};
    selected->platformId = platformId;
    selected->lastTouchedTick = nowTick;
    return *selected;
}

} // namespace

ArmOutcome arm(std::uint64_t platformId, std::uint64_t nowTick, bool join,
               std::uint64_t activitySessionId) noexcept {
    if (platformId == 0 || activitySessionId == 0) {
        return ArmOutcome::invalid;
    }
    const std::lock_guard guard(g_lock);
    Entry& entry = find_or_allocate(platformId, nowTick);
    entry.lastTouchedTick = nowTick;
    if (!join && entry.joinSessionId == activitySessionId
        && nowTick < entry.transitionUntilTick) {
        return ArmOutcome::suppressed;
    }
    const bool wasArmed = entry.awaitingClientSync;
    entry.awaitingClientSync = true;
    if (!wasArmed) {
        ++entry.revision;
    }
    entry.joinSessionId = activitySessionId;
    entry.transitionUntilTick = nowTick + kTransitionWindowMs;
    return wasArmed ? ArmOutcome::rearmed : ArmOutcome::armed;
}

bool acknowledge_join(std::uint64_t platformId, std::uint64_t activitySessionId) noexcept {
    if (platformId == 0 || activitySessionId == 0) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    Entry* entry = find(platformId);
    if (entry == nullptr || !entry->awaitingClientSync
        || entry->joinSessionId != activitySessionId) {
        return false;
    }
    entry->awaitingClientSync = false;
    ++entry->revision;
    return true;
}

bool awaiting_client_sync(std::uint64_t platformId) noexcept {
    if (platformId == 0) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    const Entry* entry = find(platformId);
    return entry != nullptr && entry->awaitingClientSync;
}

std::uint64_t revision(std::uint64_t platformId) noexcept {
    if (platformId == 0) {
        return 0;
    }
    const std::lock_guard guard(g_lock);
    const Entry* entry = find(platformId);
    return entry == nullptr ? 0 : entry->revision;
}

void reset() noexcept {
    const std::lock_guard guard(g_lock);
    g_entries = {};
}

} // namespace dawn::server::bap::encrypted::push::activity::spawn_sync
