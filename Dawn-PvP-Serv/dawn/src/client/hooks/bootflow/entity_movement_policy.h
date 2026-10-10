#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::hooks::bootflow::entity_movement_policy {

inline constexpr std::uint32_t kPriorityMask = 0x01FF8000U;
// keep one priority step for native combat feedback; the native sort compares only these ten bits
inline constexpr std::uint32_t kMovementPriority = 1022U << 15;

constexpr std::uint32_t promote_movement(std::uint32_t packed) noexcept {
    return (packed & kPriorityMask) >= kMovementPriority
        ? packed : (packed & ~kPriorityMask) | kMovementPriority;
}
inline constexpr std::uint64_t kFallbackWaitMs = 250;
inline constexpr std::uint64_t kNativeWaitMs = 50;
inline constexpr std::uint64_t kContinuityGapMs = 2000;
inline constexpr std::size_t kSlots = 128;

struct Clock {
    bool valid{};
    std::uint64_t now{};
};

struct Key {
    const void* lane{};
    const void* manager{};
    const void* descriptor{};
    std::uint32_t token{};
    std::uint32_t view{};
    std::uint32_t channel{};
    bool operator==(const Key&) const = default;
};

struct Decision {
    bool overdue{};
    bool stampAdvanced{};
    bool advancedAfterBoost{};
    bool nativeAgeKnown{};
    std::uint64_t ageMs{};
};

struct Entry {
    Key key{};
    std::uint64_t since{};
    std::uint64_t seenAt{};
    std::uint64_t stamp{};
    bool seen{};
    bool boosted{};
};

struct Policy {
    std::array<Entry, kSlots> entries{};

    void begin(const void* lane, std::uint32_t channel) noexcept {
        for (auto& entry : entries) {
            if (entry.key.lane == lane && entry.key.channel == channel) {
                entry.seen = false;
            }
        }
    }

    [[nodiscard]] Decision observe(const Key& key,
                                   std::uint64_t stamp,
                                   std::uint64_t now,
                                   Clock clock = {}) noexcept {
        Decision result{};
        if (clock.valid && clock.now >= stamp) {
            result.nativeAgeKnown = true;
            result.ageMs = clock.now - stamp;
        }
        Entry* available = nullptr;
        for (auto& entry : entries) {
            if (entry.key == key) {
                const bool discontinuity = now < entry.seenAt
                                           || now - entry.seenAt > kContinuityGapMs;
                result.stampAdvanced = !discontinuity && stamp > entry.stamp;
                result.advancedAfterBoost = result.stampAdvanced && entry.boosted;
                if (entry.stamp != stamp || discontinuity) {
                    entry.since = now;
                    entry.boosted = false;
                }
                entry.stamp = stamp;
                entry.seenAt = now;
                entry.seen = true;
                if (discontinuity) {
                    return result;
                }
                result.overdue = result.nativeAgeKnown
                                     ? result.ageMs >= kNativeWaitMs
                                     : now >= entry.since && now - entry.since >= kFallbackWaitMs;
                return result;
            }
            if (entry.key.lane == nullptr
                || (now >= entry.seenAt && now - entry.seenAt > kContinuityGapMs)) {
                available = &entry;
            }
        }
        if (available != nullptr) {
            *available = {key, now, now, stamp, true, false};
        }
        return result;
    }

    void mark_boost(const Key& key) noexcept {
        for (auto& entry : entries) {
            if (entry.key == key) {
                entry.boosted = true;
                return;
            }
        }
    }

    void finish(const void* lane, std::uint32_t channel) noexcept {
        for (auto& entry : entries) {
            if (entry.key.lane == lane && entry.key.channel == channel && !entry.seen) {
                entry = {};
            }
        }
    }
};

} // namespace dawn::client::hooks::bootflow::entity_movement_policy
