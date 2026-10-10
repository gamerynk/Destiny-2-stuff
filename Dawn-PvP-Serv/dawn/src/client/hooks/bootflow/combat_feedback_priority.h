#pragma once

#include "entity_movement_fairness.h"

namespace dawn::client::hooks::bootflow::combat_feedback_priority {
using entity_movement_fairness::read;
using entity_movement_fairness::Clock;
inline std::atomic<std::uintptr_t> imageBase{};
inline SRWLOCK statsLock = SRWLOCK_INIT;
inline std::uint64_t gathers{}, critical{}, promoted{}, maxAge{}, lastReport{};
inline std::uint64_t melee{}, grouped{}, deaths{}, meleeMaxAge{};
inline std::uint64_t incidents{}, incidentsPromoted{}, incidentMaxAge{};
constexpr std::size_t kSlots = 2047;
constexpr std::size_t kBurst = 8;
constexpr std::uint64_t kGroupWaitMs = 50;
constexpr std::size_t kIncidentBurst = 2;
constexpr std::uint64_t kIncidentWaitMs = 50;
enum class Kind { other, feedback, melee, action, death, dependency, incident };

// resolve classes through the pinned native registry; sobject_message has a shorter vtable,
// its apply method is +68 rather than +70
inline Kind event_type(const void* registry, std::uint32_t type) noexcept {
    const void* definition{}; const void* vtable{}; std::uintptr_t dispatch{};
    const auto image = imageBase.load(std::memory_order_acquire);
    if (!image || type >= 32 || !read(registry, 0x30 + type * 8, definition)
        || !read(definition, 0, vtable) || !read(vtable, 0x68, dispatch)) return Kind::other;
    if (dispatch == image + 0x1723570) return Kind::dependency;
    if (!read(vtable, 0x70, dispatch)) return Kind::other;
    if (dispatch == image + 0xB7D690 || dispatch == image + 0xB7D710
        || dispatch == image + 0xB7D610) return Kind::feedback;
    if (dispatch == image + 0xEF60A0) return Kind::melee;
    if (dispatch == image + 0xB7D790 || dispatch == image + 0xB7D810
        || dispatch == image + 0xB7DAD0) return Kind::death;
    if (dispatch == image + 0x1723750 || dispatch == image + 0x16E6AA0) return Kind::action;
    if (dispatch == image + 0x17238C0) return Kind::dependency;
    if (dispatch == image + 0x16E6C80) return Kind::incident;
    return Kind::other;
}

inline bool pending_combat(const void* event, const void* registry, std::uint32_t peer,
                           bool inGroup, Clock clock, std::uint64_t& stamp, Kind& kind) noexcept {
    std::uint32_t recipients{}, inFlight{}, type{}; std::int32_t lifetime{};
    if (peer >= 32 || !clock.valid
        || !read(event, 0x50, recipients) || (recipients & (1U << peer)) == 0
        || !read(event, 0x54, inFlight) || (inFlight & (1U << peer)) != 0
        || !read(event, 4, type) || !read(event, 0x10, stamp) || clock.now < stamp
        || !read(event, 0x18, lifetime) || lifetime < -1
        || (lifetime >= 0 && clock.now - stamp >= static_cast<std::uint64_t>(lifetime))) return false;
    kind = event_type(registry, type);
    // give grouped dependencies one normal send interval before promotion; never revive expired
    // events or bypass the native FIFO
    if (kind == Kind::incident) return clock.now - stamp >= kIncidentWaitMs;
    return kind != Kind::other && (kind != Kind::dependency
        || (inGroup && clock.now - stamp >= kGroupWaitMs));
}

inline bool identify(void* lane, std::uint32_t channel, std::uint32_t laneIndex,
                     std::uint32_t packed, std::uint64_t& stamp,
                     std::size_t* remaining = nullptr, Kind* selected = nullptr,
                     Clock clock = entity_movement_fairness::read_clock()) noexcept {
    // a grouped candidate names the FIFO head; promote that head, never a later event
    const auto index = packed & 0x7FFFU;
    const auto operation = (packed >> 25) & 3U;
    const auto image = imageBase.load(std::memory_order_acquire);
    const void* manager{}; const void* domain{}; const void* registry{}; const void* event{};
    std::uintptr_t vtable{}; std::uint32_t peer{}, identity{};
    std::uint8_t enabled{}, failed{}; std::int32_t group{}; Kind kind{};
    if (!image || channel > 1 || laneIndex > 3 || index >= kSlots || operation > 1
        || ((packed >> 29) & 3U) != channel || ((packed >> 27) & 3U) != laneIndex
        || !read(lane, 0, vtable) || vtable != image + 0x1C9AE50
        || !read(lane, 9, failed) || failed || !read(lane, 0xA, enabled) || enabled != 1
        || !read(lane, 0xC, peer) || peer >= 32
        || !read(lane, 0x20, manager) || !read(manager, 8, domain)
        || !read(domain, 0x18, registry)
        || !read(manager, 0x9D0 + index * 8, event)
        || !read(event, 0x60, identity) || identity != index
        || !read(event, 8, group)
        || (operation == 0 ? group != -1 : (group < 0 || group >= 330))) return false;
    bool foundIncident{};
    std::uint64_t incidentStamp{};
    std::size_t n{};
    for (; event && n < 1024; ++n) {
        if (remaining) { if (*remaining == 0) return false; --*remaining; }
        std::int32_t currentGroup{};
        if (!read(event, 8, currentGroup)) return false;
        if (currentGroup == group && pending_combat(event, registry, peer, operation == 1, clock, stamp, kind)) {
            if (kind != Kind::incident || operation == 0) {
                if (selected) *selected = kind;
                return true;
            }
            if (!foundIncident) { foundIncident = true; incidentStamp = stamp; }
        }
        if (operation == 0) return false;
        const void* next{};
        if (!read(event, 0x58, next) || next == event) return false;
        event = next;
    }
    if (event || !foundIncident) return false;
    stamp = incidentStamp;
    if (selected) *selected = Kind::incident;
    return true;
}

inline bool promote(std::uint32_t* output, std::uint32_t expected) noexcept {
    __try {
        if (!output || *output != expected) return false;
        *output = expected | entity_movement_policy::kPriorityMask;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Sample {
    std::uint64_t found{}, changed{}, oldest{}, melee{}, grouped{}, deaths{}, meleeOldest{};
    std::uint64_t incidents{}, incidentsChanged{}, incidentOldest{};
};
inline Sample prioritize(void* lane, std::uint32_t channel, std::uint32_t laneIndex,
                         std::int32_t capacity, std::int32_t count, std::uint32_t* output,
                         Clock clock) noexcept {
    Sample sample{};
    if (!lane || channel > 1 || laneIndex > 3 || capacity < 0 || capacity > 2000
        || count < 0 || count > capacity || (count && !output) || !clock.valid) return sample;
    struct Candidate { std::uint32_t* output{}; std::uint32_t packed{}; std::uint64_t stamp{}; };
    std::array<Candidate, kBurst> chosen{};
    std::array<Candidate, kIncidentBurst> notifications{};
    std::size_t used{}, notificationsUsed{}, remaining = 4096;
    const auto selectOldest = [](auto& candidates, std::size_t& size, Candidate candidate) noexcept {
        std::size_t at = size;
        while (at && candidates[at - 1].stamp > candidate.stamp) --at;
        if (at == candidates.size()) return;
        const auto end = size < candidates.size() ? size++ : candidates.size() - 1;
        for (auto j = end; j > at; --j) candidates[j] = candidates[j - 1];
        candidates[at] = candidate;
    };
    for (std::int32_t i = 0; i < count && remaining; ++i) {
        std::uint32_t packed{}; std::uint64_t stamp{}; Kind kind{};
        if (!read(output, static_cast<std::size_t>(i) * 4, packed)
            || !identify(lane, channel, laneIndex, packed, stamp, &remaining, &kind, clock)) continue;
        ++sample.found;
        const auto age = clock.now - stamp;
        if (age > sample.oldest) sample.oldest = age;
        if (kind == Kind::melee) { ++sample.melee; if (age > sample.meleeOldest) sample.meleeOldest = age; }
        if (kind == Kind::dependency) ++sample.grouped;
        if (kind == Kind::death) ++sample.deaths;
        if (kind == Kind::incident) {
            ++sample.incidents;
            if (age > sample.incidentOldest) sample.incidentOldest = age;
        }
        if ((packed & entity_movement_policy::kPriorityMask) == entity_movement_policy::kPriorityMask) continue;
        const Candidate candidate{output + i, packed, stamp};
        if (kind == Kind::incident) selectOldest(notifications, notificationsUsed, candidate);
        else selectOldest(chosen, used, candidate);
    }
    for (std::size_t i = 0; i < used; ++i)
        if (promote(chosen[i].output, chosen[i].packed)) ++sample.changed;
    for (std::size_t i = 0; i < notificationsUsed && used + i < kBurst; ++i)
        if (promote(notifications[i].output, notifications[i].packed)) {
            ++sample.changed; ++sample.incidentsChanged;
        }
    return sample;
}

inline void apply(void* lane, std::uint32_t channel, std::uint32_t laneIndex,
                  std::int32_t capacity, std::int32_t count, std::uint32_t* output) noexcept {
    const auto sample = prioritize(lane, channel, laneIndex, capacity, count, output,
                                   entity_movement_fairness::read_clock());
    if (!TryAcquireSRWLockExclusive(&statsLock)) return;
    ++gathers; critical += sample.found; promoted += sample.changed;
    melee += sample.melee; grouped += sample.grouped; deaths += sample.deaths;
    incidents += sample.incidents; incidentsPromoted += sample.incidentsChanged;
    if (sample.incidentOldest > incidentMaxAge) incidentMaxAge = sample.incidentOldest;
    if (sample.oldest > maxAge) maxAge = sample.oldest;
    if (sample.meleeOldest > meleeMaxAge) meleeMaxAge = sample.meleeOldest;
    const auto now = GetTickCount64();
    std::array<char, 768> line{};
    int length{};
    if (now < lastReport || now - lastReport >= 5000) {
        length = std::snprintf(line.data(), line.size(),
            "ev=combat_feedback stage=gather gathers=%llu critical=%llu promoted=%llu max_pending_age_ms=%llu"
            " melee=%llu melee_max_pending_age_ms=%llu grouped=%llu hit_death=%llu"
            " incidents=%llu incidents_promoted=%llu incident_max_pending_age_ms=%llu burst=8 incident_burst=2 mutation=priority_only",
            static_cast<unsigned long long>(gathers), static_cast<unsigned long long>(critical),
            static_cast<unsigned long long>(promoted), static_cast<unsigned long long>(maxAge),
            static_cast<unsigned long long>(melee), static_cast<unsigned long long>(meleeMaxAge),
            static_cast<unsigned long long>(grouped), static_cast<unsigned long long>(deaths),
            static_cast<unsigned long long>(incidents), static_cast<unsigned long long>(incidentsPromoted),
            static_cast<unsigned long long>(incidentMaxAge));
        gathers = critical = promoted = maxAge = melee = grouped = deaths = meleeMaxAge = 0; lastReport = now;
        incidents = incidentsPromoted = incidentMaxAge = 0;
    }
    ReleaseSRWLockExclusive(&statsLock);
    if (length > 0 && static_cast<std::size_t>(length) < line.size())
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(length)});
}
} // namespace dawn::client::hooks::bootflow::combat_feedback_priority
