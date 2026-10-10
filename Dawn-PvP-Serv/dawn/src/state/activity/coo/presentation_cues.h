#pragma once
#include "executor.h"

namespace dawn::state::activity::coo {
struct PresentationAction final {
    Operation operation;
    std::uint32_t value, delayMs{};
};
struct PresentationCue final {
    std::uint32_t event;
    std::uint8_t cycles;
    std::span<const PresentationAction> actions;
};
template<class Target>
void apply_presentation(std::span<const PresentationAction> actions, Target& target, std::uint64_t now) noexcept {
    for (const auto& action : actions) {
        if (action.operation == Operation::objective) { target.set_objective(action.value); }
        else if (action.operation == Operation::dialogue && action.value <= UINT8_MAX) {
            target.enqueue(static_cast<std::uint8_t>(action.value), now, action.delayMs);
        }
    }
}
template<class Target>
void present_event(std::span<const PresentationCue> cues, std::uint32_t event, std::uint8_t cycle,
                   Target& target, std::uint64_t now) noexcept {
    if (cycle > 8) { return; }
    for (const auto& cue : cues) {
        if (cue.event == event && (cue.cycles == 0 || (cycle > 0 && (cue.cycles & (1U << (cycle - 1))) != 0))) {
            apply_presentation(cue.actions, target, now);
        }
    }
}
} // namespace dawn::state::activity::coo
