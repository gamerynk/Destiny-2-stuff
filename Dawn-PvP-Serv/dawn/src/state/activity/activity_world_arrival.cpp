#include <Windows.h>

#include <atomic>

#include "runtime.h"

namespace dawn::state::activity {
namespace {

std::atomic<WorldPhase> g_phase{WorldPhase::idle};
std::atomic<std::uint64_t> g_transitionTick{};

} // namespace

void note_world_phase(WorldPhase phase) noexcept {
    const WorldPhase previous = g_phase.load(std::memory_order_relaxed);
    // stamp the tick before the phase so a reader that sees transitioning never gets a stale one
    if (phase == WorldPhase::transitioning && previous != WorldPhase::transitioning) {
        g_transitionTick.store(GetTickCount64(), std::memory_order_relaxed);
    }
    g_phase.store(phase, std::memory_order_relaxed);
}

WorldPhase world_phase() noexcept {
    return g_phase.load(std::memory_order_relaxed);
}

std::uint64_t world_transition_age() noexcept {
    if (g_phase.load(std::memory_order_relaxed) != WorldPhase::transitioning) {
        return 0;
    }
    const std::uint64_t started = g_transitionTick.load(std::memory_order_relaxed);
    const std::uint64_t now = GetTickCount64();
    return now > started ? now - started : 0;
}

} // namespace dawn::state::activity
