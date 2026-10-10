#include "player_position.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace dawn::client::player::position {
namespace {

namespace teleport = hooks::teleport;

SRWLOCK g_positionLock = SRWLOCK_INIT;
class PositionLock final {
public:
    explicit PositionLock(bool write) noexcept : write_(write) {
        if (write_) AcquireSRWLockExclusive(&g_positionLock);
        else AcquireSRWLockShared(&g_positionLock);
    }
    ~PositionLock() {
        if (write_) ReleaseSRWLockExclusive(&g_positionLock);
        else ReleaseSRWLockShared(&g_positionLock);
    }
    PositionLock(const PositionLock&) = delete;
    PositionLock& operator=(const PositionLock&) = delete;
private:
    bool write_;
};
// guarded together with the component and presence flag so concurrent writers can't mix lanes
teleport::Vector g_position{};
std::atomic_bool g_present{false};
// found on the sync tick; a native body lookup supplies it when a stationary player's
// publisher has not ticked yet
std::atomic<void*> g_component{nullptr};

[[nodiscard]] bool publish_from(void* component) noexcept {
    teleport::Vector position{};
    // the body is gone at rest and during a load, so the last position stands; a player who
    // cannot be read has not moved
    if (!teleport::read_position(component, position)) {
        return false;
    }
    g_position = position;
    g_present.store(true, std::memory_order_release);
    return true;
}

} // namespace

void observe(void* component) noexcept {
    const PositionLock lock(true);
    if (component == nullptr) {
        return;
    }
    void* const known = g_component.load(std::memory_order_relaxed);
    if (known == component) {
        if (!publish_from(component)) g_component.store(nullptr, std::memory_order_relaxed);
        return;
    }
    // pay the ownership test only until the component is known; the frame poll drops a stale
    // one, which lets a new destination's component be found
    if (known != nullptr || !teleport::owns_local_player(component)) {
        return;
    }
    // ownership alone can match an unused low datum index during a load; admit only a typed,
    // readable local body so a rejected candidate doesn't block the native fallback
    if (publish_from(component)) g_component.store(component, std::memory_order_relaxed);
}

void poll() noexcept {
    const PositionLock lock(true);
    void* component = g_component.load(std::memory_order_relaxed);
    if (component != nullptr) {
        if (publish_from(component)) return;
        g_component.store(nullptr, std::memory_order_relaxed);
        if (!teleport::owns_local_player(component))
            g_present.store(false, std::memory_order_release);
    }
    // retry this frame after a cached component retires, including a stationary player's body
    component = teleport::local_player_component();
    if (component != nullptr && publish_from(component))
        g_component.store(component, std::memory_order_relaxed);
}

void reset() noexcept {
    const PositionLock lock(true);
    g_component.store(nullptr, std::memory_order_relaxed);
    g_present.store(false, std::memory_order_release);
}

Snapshot snapshot() noexcept {
    const PositionLock lock(false);
    Snapshot value{};
    if (!g_present.load(std::memory_order_acquire)) {
        return value;
    }
    value.position = g_position;
    value.present = true;
    return value;
}

} // namespace dawn::client::player::position
