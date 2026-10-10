#include "ui_transition_animation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <imgui.h>

namespace dawn::core::ui::animation::transition {
namespace {

constexpr std::size_t kTransitionCapacity = 256;
constexpr float kMaximumDeltaSeconds = 0.1F;
constexpr float kSnapDistance = 1.0F / 255.0F;
constexpr float kMinimumValue = 0.0F;
constexpr float kMaximumValue = 1.0F;

struct Slot {
    ImGuiID id{};
    Lane lane{};
    float value{};
    std::uint32_t lastUpdateFrame{};
    std::uint32_t lastSeenFrame{};
    bool occupied{};
};

std::array<Slot, kTransitionCapacity> g_slots{};

[[nodiscard]] float normalize(float value) noexcept {
    return (std::clamp)(value, kMinimumValue, kMaximumValue);
}

[[nodiscard]] Slot&
find_or_claim(ImGuiID id, Lane lane, std::uint32_t frame, float initialValue) noexcept {
    Slot* available = nullptr;
    Slot* oldest = &g_slots.front();
    std::uint32_t oldestAge = 0;

    for (Slot& slot : g_slots) {
        if (slot.occupied && slot.id == id && slot.lane == lane) {
            return slot;
        }
        if (!slot.occupied && available == nullptr) {
            available = &slot;
            continue;
        }
        if (slot.occupied) {
            const std::uint32_t age = frame - slot.lastSeenFrame;
            if (age > oldestAge) {
                oldest = &slot;
                oldestAge = age;
            }
        }
    }

    Slot& claimed = available != nullptr ? *available : *oldest;
    claimed = {id, lane, normalize(initialValue), frame, frame, true};
    return claimed;
}

[[nodiscard]] float advance(float value, float target, float rate, float deltaSeconds) noexcept {
    // exponential decay takes the same time at any frame rate; plain rate * delta hits a
    // blend of 1 below 17 fps and skips the transition
    const float blend = 1.0F - std::exp(-(std::max)(rate, kMinimumValue) * deltaSeconds);
    const float advanced = value + ((target - value) * blend);
    if (std::fabs(target - advanced) <= kSnapDistance) {
        return target;
    }
    return advanced;
}

} // namespace

float update(ImGuiID id, Lane lane, bool enabled, Rates rates, float initialValue) noexcept {
    const float target = enabled ? kMaximumValue : kMinimumValue;
    if (ImGui::GetCurrentContext() == nullptr) {
        return target;
    }

    const std::uint32_t frame = static_cast<std::uint32_t>(ImGui::GetFrameCount());
    Slot& slot = find_or_claim(id, lane, frame, initialValue);
    slot.lastSeenFrame = frame;
    if (slot.lastUpdateFrame == frame) {
        return slot.value;
    }

    const float deltaSeconds =
        (std::clamp)(ImGui::GetIO().DeltaTime, kMinimumValue, kMaximumDeltaSeconds);
    const float rate = target > slot.value ? rates.risePerSecond : rates.fallPerSecond;
    slot.value = advance(slot.value, target, rate, deltaSeconds);
    slot.lastUpdateFrame = frame;
    return slot.value;
}

void reset() noexcept {
    g_slots = {};
}

} // namespace dawn::core::ui::animation::transition
