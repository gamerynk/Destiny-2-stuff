#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawn::client::hooks::bootflow::remote_facing_policy {

struct Vector { float x{}, y{}, z{}, w{}; };
inline bool valid(Vector v) noexcept {
    const auto norm = v.x*v.x + v.y*v.y + v.z*v.z;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::isfinite(v.w) && norm >= 0.9F && norm <= 1.1F && std::abs(v.w) <= 1.0F;
}
inline bool different(Vector a, Vector b) noexcept {
    return std::abs(a.x-b.x)>0.00001F || std::abs(a.y-b.y)>0.00001F
        || std::abs(a.z-b.z)>0.00001F;
}

struct Key {
    std::uintptr_t component{};
    std::uint32_t owner{}, legacy{UINT32_MAX}, asset{};
    std::uint8_t controlEpoch{};
    bool operator==(const Key&) const = default;
};
struct State {
    bool identity{}, authority{}, suppressed{};
    std::uint8_t classification{}, controlEpoch{}, transition{};
    std::uint32_t vehicle{UINT32_MAX}, flags{};
    Vector received{}, control{}, secondary{}, facing{}, secondaryFacing{};
};
inline bool supported(const State& s) noexcept {
    return s.identity && !s.authority && !s.suppressed && s.classification == 1
        && s.controlEpoch >= 2 && s.controlEpoch <= 15 && s.vehicle == UINT32_MAX
        && s.flags == 0 && s.transition == 0 && valid(s.received)
        && valid(s.control) && valid(s.secondary) && valid(s.facing) && valid(s.secondaryFacing);
}
inline bool eligible(const State& s) noexcept {
    return supported(s) && (different(s.received,s.control) || different(s.received,s.secondary)
        || different(s.received,s.facing) || different(s.received,s.secondaryFacing));
}

struct Pending { Key key{}; std::uint64_t stamp{}; };
struct Queue {
    static constexpr std::size_t capacity = 64;
    std::array<Pending,capacity> entries{};
    static bool fresh(const Pending& e, std::uint64_t now) noexcept {
        return e.key.component && now >= e.stamp && now-e.stamp <= 1000;
    }
    bool push(Key key, std::uint64_t stamp) noexcept {
        if (!key.component || !key.owner || key.owner >= 0x80000000U
            || key.legacy == UINT32_MAX || key.asset != 0x80C0C51AU
            || key.controlEpoch < 2 || key.controlEpoch > 15) return false;
        for (auto& e:entries) if (e.key == key) { e.stamp=stamp; return true; }
        for (auto& e:entries) if (!e.key.component || !fresh(e,stamp)) { e={key,stamp}; return true; }
        return false;
    }
};
inline bool applies(const Pending& input, const Key& current, const State& state,
                    std::uint64_t now) noexcept {
    return Queue::fresh(input,now) && input.key == current
        && current.controlEpoch == state.controlEpoch && eligible(state);
}

} // namespace dawn::client::hooks::bootflow::remote_facing_policy
