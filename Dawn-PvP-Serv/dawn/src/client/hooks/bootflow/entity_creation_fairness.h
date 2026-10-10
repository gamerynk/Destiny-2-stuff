#pragma once

#include "entity_lifecycle_fairness.h"

// the native creation age curve tops out at .98 (1002 before scaling, 971 captured), below our
// movement floor of 1022, so aged creation needs promoting. Only promote groups the native gather admitted
namespace dawn::client::hooks::bootflow::entity_creation_fairness {
namespace memory = entity_movement_fairness;
namespace lifecycle = entity_lifecycle_fairness;
inline constexpr std::uint64_t kWaitMs = 250;
inline constexpr std::size_t kBurst = 2;
inline std::atomic_bool configured{};
inline std::atomic<std::uint32_t> scanCursor{};

inline bool configure(std::uintptr_t image) noexcept {
    configured.store(false, std::memory_order_release);
    constexpr std::array<std::uint8_t, 8> creationTest{
        0x44,0x84,0x6A,0x38,0x41,0x0F,0xB6,0xED};
    constexpr std::array<std::uint8_t, 13> creationCurve{
        0x45,0x84,0xE4,0x74,0x48,0xF3,0x0F,0x10,0x1D,0x9B,0x82,0x4A,0x00};
    std::array<std::uint8_t, creationTest.size()> test{};
    std::array<std::uint8_t, creationCurve.size()> curve{};
    float ceiling{};
    const bool valid = image && lifecycle::imageBase.load(std::memory_order_acquire) == image
        && memory::read(reinterpret_cast<const void*>(image), 0x171142C, test) && test == creationTest
        && memory::read(reinterpret_cast<const void*>(image), 0x1727760, curve) && curve == creationCurve
        && memory::read(reinterpret_cast<const void*>(image), 0x1BCFA08, ceiling) && ceiling == 0.98F;
    configured.store(valid, std::memory_order_release);
    return valid;
}

inline std::uint64_t creation_age(const std::byte* row, std::int32_t view,
                                  memory::Clock clock) noexcept {
    const std::byte* aux{}; std::uint32_t views{}; std::uint16_t global{}, flags{};
    std::uint64_t stamp{};
    if (!memory::read(row, 0x50, global) || (global & 4U)
        || !memory::read(row, 0x60, aux) || !aux || !memory::read(aux, 4, views)
        || !(views & (1U << view))) return 0;
    const auto* descriptor = aux + 0x18 + static_cast<std::size_t>(view) * 0x40;
    // children need not have gathered bit7 (native gathers their root); never confuse removal (bit2) with creation (bit0)
    if (!memory::read(descriptor, 0x38, flags) || (flags & 5U) != 1U
        || !memory::read(descriptor, 0x20, stamp) || clock.now < stamp) return 0;
    return clock.now - stamp;
}

inline std::uint64_t group_age(const std::byte* rows, const void* manager,
                               const std::byte* root, std::uint32_t rootToken,
                               std::int32_t view, memory::Clock clock,
                               std::size_t& budget) noexcept {
    const auto rootAge = creation_age(root, view, clock);
    if (rootAge >= kWaitMs) return rootAge;
    struct Link { std::uint32_t token, parent; };
    std::array<Link, lifecycle::kTreeLimit> pending{};
    std::array<std::uint32_t, lifecycle::kTreeLimit> visited{};
    std::size_t size{}, seen{}; std::uint64_t oldest{}; std::uint32_t first{};
    if (!memory::read(root, 0x48, first)) return 0;
    if (first != UINT32_MAX) pending[size++] = {first, rootToken};
    while (size) {
        if (!budget || seen == visited.size()) return 0;
        --budget;
        const auto link = pending[--size];
        if (link.token == rootToken) return 0;
        for (std::size_t i = 0; i < seen; ++i) if (visited[i] == link.token) return 0;
        visited[seen++] = link.token;
        const auto* row = lifecycle::resolve(rows, manager, link.token);
        std::uint8_t kind{}, owner{}; std::uint32_t parent{}, child{}, sibling{};
        if (!row || !memory::read(row, 0, kind) || kind != 0
            || !memory::read(row, 1, owner) || owner != 0xFF
            || !memory::read(row, 0xC, parent) || parent != link.parent
            || !memory::read(row, 0x44, sibling) || !memory::read(row, 0x48, child)) return 0;
        const auto age = creation_age(row, view, clock);
        if (age > oldest) oldest = age;
        if (sibling != UINT32_MAX) {
            if (size == pending.size()) return 0;
            pending[size++] = {sibling, parent};
        }
        if (child != UINT32_MAX) {
            if (size == pending.size()) return 0;
            pending[size++] = {child, link.token};
        }
    }
    return oldest;
}

inline std::uint64_t eligible_age(void* lane, std::uint32_t packed, memory::Clock clock,
                                  std::uint32_t channel, std::size_t& budget) noexcept {
    const auto* rows = memory::table.load(std::memory_order_acquire);
    const auto image = lifecycle::imageBase.load(std::memory_order_acquire);
    const auto index = packed & 0x7FFFU;
    std::uintptr_t vtable{}; const void* manager{}; std::int32_t view{};
    std::uint8_t enabledA{}, enabledB{};
    if (!configured.load(std::memory_order_acquire) || !rows || !image || !clock.valid
        || channel > 2 || index > 0x1FFF || ((packed >> 29) & 3U) != channel
        || ((packed >> 27) & 3U) != 2 || !memory::read(lane, 0, vtable) || vtable != image + 0x1C9ADD8
        || !memory::read(lane, 9, enabledA) || !enabledA
        || !memory::read(lane, 10, enabledB) || !enabledB
        || !memory::read(lane, 0x10, manager) || !manager
        || !memory::read(lane, 0xC, view) || view < 0 || view >= 32) return 0;
    std::int16_t slot{};
    if (!memory::read(manager, 0x114 + index * 6, slot) || slot < 0 || slot >= 1024) return 0;
    const auto* row = rows + static_cast<std::size_t>(slot) * 0x70;
    std::uint8_t kind{}, owner{}; std::uint32_t token{}, parent{}, views{};
    std::uint16_t global{}, flags{}; const std::byte* aux{};
    if (!memory::read(row, 0, kind) || kind != 0 || !memory::read(row, 1, owner) || owner != 0xFF
        || !memory::read(row, 8, token) || (token & 0x1FFFU) != index
        || lifecycle::resolve(rows, manager, token) != row
        || !memory::read(row, 0xC, parent) || parent != UINT32_MAX
        || !memory::read(row, 0x50, global) || (global & 0x204U)
        || !memory::read(row, 0x60, aux) || !aux || !memory::read(aux, 4, views)
        || !(views & (1U << view))
        || !memory::read(aux, 0x18 + static_cast<std::size_t>(view) * 0x40 + 0x38, flags)
        || (flags & 0x84U) != 0x80U) return 0;
    return group_age(rows, manager, row, token, view, clock, budget);
}

inline std::size_t apply(void* lane, std::uint32_t channel, std::uint32_t laneIndex,
                          std::int32_t capacity, std::int32_t count, std::uint32_t* output,
                          memory::Clock clock) noexcept {
    if (!configured.load(std::memory_order_acquire)
        || !lane || !output || !clock.valid || channel > 2 || laneIndex != 2
        || count <= 0 || count > capacity || capacity > 2000) return 0;
    struct Candidate { std::uint64_t age{}; std::uint32_t packed{}; std::int32_t index{}; };
    std::array<Candidate, kBurst> selected{};
    std::size_t budget = 256;
    const auto start = scanCursor.fetch_add(1, std::memory_order_relaxed) % static_cast<std::uint32_t>(count);
    for (std::int32_t step = 0; step < count && budget; ++step) {
        --budget;
        const auto i = static_cast<std::int32_t>((start + step) % count);
        std::uint32_t packed{};
        if (!memory::read(output, static_cast<std::size_t>(i) * 4, packed)
            || (packed & memory::kPriorityMask) == memory::kPriorityMask) continue;
        const auto age = eligible_age(lane, packed, clock, channel, budget);
        if (age < kWaitMs) continue;
        Candidate candidate{age, packed, i};
        for (auto& entry : selected) {
            if (candidate.age > entry.age) {
                const auto displaced = entry; entry = candidate; candidate = displaced;
            }
        }
    }
    std::size_t raised{};
    for (const auto& entry : selected)
        if (entry.age && combat_feedback_priority::promote(output + entry.index, entry.packed)) ++raised;
    return raised;
}
} // namespace dawn::client::hooks::bootflow::entity_creation_fairness
