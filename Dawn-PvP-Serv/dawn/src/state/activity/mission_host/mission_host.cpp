#include "mission_host.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <mutex>

#include "../../../core/logging/log.h"
#include "../runtime.h"

namespace dawn::state::activity::mission_host {
namespace {

constexpr std::uint64_t kIdleReleaseMs = 30'000;

struct Slot final {
    std::uint64_t session{};
    std::uint64_t run{};
    std::uint64_t lastBindTick{};
    std::uint64_t refusedSession{};
    int region{-1};
    int regionRank{-1};
    bool armed{};
    std::array<std::uint64_t, 12> members{};
    std::uint64_t authority{};
};

std::mutex g_mutex;
Slot g_slot;
std::uint64_t g_lastRun{};

template <class... Args>
void log(core::log::Level level, const char* format, Args... args) noexcept {
    std::array<char, 256> line{};
    const int written = std::snprintf(line.data(), line.size(), format, args...);
    if (written > 0) {
        core::log::write(core::log::Channel::server, level,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

// the run doubles as the executor's receipt owner, so it must never repeat in a process
void claim_locked(std::uint64_t sessionId, std::uint64_t now) noexcept {
    const std::uint64_t candidate = now > g_lastRun ? now : g_lastRun + 1;
    g_lastRun = candidate;
    g_slot = {};
    g_slot.session = sessionId;
    g_slot.run = candidate;
    g_slot.lastBindTick = now;
    log(core::log::Level::info, "ev=mission_host stage=bind session=0x%llX run=%llu idle_ms=%llu",
        static_cast<unsigned long long>(sessionId), static_cast<unsigned long long>(candidate),
        static_cast<unsigned long long>(kIdleReleaseMs));
}

} // namespace

std::uint64_t bind(std::uint64_t sessionId, std::uint64_t now) noexcept {
    if (sessionId == 0) {
        return 0;
    }
    const std::lock_guard lock(g_mutex);
    if (g_slot.session == sessionId) {
        g_slot.lastBindTick = now;
        return g_slot.run;
    }
    const bool holderLive = g_slot.session != 0 && contains(g_slot.session);
    if (holderLive && now - g_slot.lastBindTick < kIdleReleaseMs) {
        if (g_slot.refusedSession != sessionId) {
            g_slot.refusedSession = sessionId;
            log(core::log::Level::warn,
                "ev=mission_host stage=bind result=refused session=0x%llX holder=0x%llX run=%llu",
                static_cast<unsigned long long>(sessionId),
                static_cast<unsigned long long>(g_slot.session),
                static_cast<unsigned long long>(g_slot.run));
        }
        return 0;
    }
    if (g_slot.session != 0) {
        log(core::log::Level::info,
            holderLive
                ? "ev=mission_host stage=release reason=idle session=0x%llX run=%llu idle_ms=%llu"
                : "ev=mission_host stage=release reason=session_gone session=0x%llX run=%llu idle_ms=%llu",
            static_cast<unsigned long long>(g_slot.session),
            static_cast<unsigned long long>(g_slot.run),
            static_cast<unsigned long long>(now - g_slot.lastBindTick));
    }
    claim_locked(sessionId, now);
    return g_slot.run;
}

std::uint64_t run() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_slot.run;
}

std::uint64_t session() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_slot.session;
}

void note_arrived(std::uint64_t sessionId) noexcept {
    const std::lock_guard lock(g_mutex);
    if (sessionId == 0 || sessionId != g_slot.session || g_slot.armed) {
        return;
    }
    g_slot.armed = true;
    log(core::log::Level::info, "ev=mission_host stage=armed session=0x%llX run=%llu region=%d",
        static_cast<unsigned long long>(sessionId), static_cast<unsigned long long>(g_slot.run),
        g_slot.region);
}

bool armed() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_slot.run != 0 && g_slot.armed;
}

void note_region(std::uint64_t sessionId, int region, RegionRank rank) noexcept {
    if (region < 0 || rank == nullptr) {
        return;
    }
    const int candidate = rank(region);
    const std::lock_guard lock(g_mutex);
    if (sessionId == 0 || sessionId != g_slot.session) {
        return;
    }
    if (g_slot.region >= 0 && candidate <= g_slot.regionRank) {
        return;
    }
    const int prior = g_slot.region;
    g_slot.region = region;
    g_slot.regionRank = candidate;
    log(core::log::Level::info, "ev=mission_host stage=region run=%llu from=%d to=%d",
        static_cast<unsigned long long>(g_slot.run), prior, region);
}

int region() noexcept {
    const std::lock_guard lock(g_mutex);
    return g_slot.region;
}

void note_member(std::uint64_t sessionId, std::uint64_t platformId) noexcept {
    const std::lock_guard lock(g_mutex);
    if (sessionId == 0 || platformId == 0 || sessionId != g_slot.session) {
        return;
    }
    for (std::uint64_t& slot : g_slot.members) {
        if (slot == platformId) {
            return;
        }
        if (slot == 0) {
            slot = platformId;
            log(core::log::Level::info, "ev=mission_host stage=member run=%llu platform=%llu",
                static_cast<unsigned long long>(g_slot.run),
                static_cast<unsigned long long>(platformId));
            return;
        }
    }
}

bool member(std::uint64_t platformId) noexcept {
    const std::lock_guard lock(g_mutex);
    if (platformId == 0 || g_slot.run == 0) {
        return false;
    }
    for (const std::uint64_t slot : g_slot.members) {
        if (slot == platformId) {
            return true;
        }
    }
    return false;
}

void note_authority(std::uint64_t sessionId, std::uint64_t platformId) noexcept {
    const std::lock_guard lock(g_mutex);
    if (sessionId == 0 || platformId == 0 || sessionId != g_slot.session
        || g_slot.authority == platformId) {
        return;
    }
    g_slot.authority = platformId;
    log(core::log::Level::info, "ev=mission_host stage=authority run=%llu platform=%llu",
        static_cast<unsigned long long>(g_slot.run), static_cast<unsigned long long>(platformId));
}

bool authoritative(std::uint64_t platformId) noexcept {
    const std::lock_guard lock(g_mutex);
    return platformId != 0 && g_slot.run != 0
           && (g_slot.authority == 0 || g_slot.authority == platformId);
}

void release(std::uint64_t sessionId) noexcept {
    const std::lock_guard lock(g_mutex);
    if (sessionId == 0 || sessionId != g_slot.session) {
        return;
    }
    log(core::log::Level::info, "ev=mission_host stage=release reason=session session=0x%llX run=%llu",
        static_cast<unsigned long long>(sessionId), static_cast<unsigned long long>(g_slot.run));
    g_slot = {};
}

} // namespace dawn::state::activity::mission_host
