#pragma once

#include "entity_lifecycle_fairness.h"

namespace dawn::client::hooks::bootflow::entity_handoff_fairness {
namespace memory = entity_movement_fairness;
namespace lifecycle = entity_lifecycle_fairness;
inline constexpr std::uint64_t kWaitMs = 250;
inline constexpr std::size_t kBurst = 2;
inline std::atomic_bool configured{};
inline std::atomic<std::uint32_t> scanCursor{};

inline bool configure(std::uintptr_t image) noexcept {
    configured.store(false, std::memory_order_release);
    // 1713FD0 marks outgoing transfer bit3, 171D510 sets owner -3, and 170C080 gathers that
    // owner only for aux[0]'s destination peer
    constexpr std::array<std::uint8_t,6> mark{0x66,0x83,0x4C,0x3B,0x50,0x08};
    constexpr std::array<std::uint8_t,8> owner{0x0F,0x94,0xC0,0x04,0xFD,0x88,0x47,0x01};
    constexpr std::array<std::uint8_t,12> destination{0x0F,0xBE,0x02,0x41,0x3B,0xC0,0x0F,0x85,0x78,0x01,0,0};
    std::array<std::uint8_t,mark.size()> a{};
    std::array<std::uint8_t,owner.size()> b{};
    std::array<std::uint8_t,destination.size()> c{};
    const bool valid = image && lifecycle::imageBase.load(std::memory_order_acquire) == image
        && memory::read(reinterpret_cast<const void*>(image), 0x1714052, a) && a == mark
        && memory::read(reinterpret_cast<const void*>(image), 0x171D672, b) && b == owner
        && memory::read(reinterpret_cast<const void*>(image), 0x170C1A0, c) && c == destination;
    configured.store(valid, std::memory_order_release);
    return valid;
}

inline std::uint64_t eligible_age(void* lane, std::uint32_t packed, memory::Clock clock,
                                  std::uint32_t channel) noexcept {
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
        || !memory::read(lane, 0xC, view) || view < 0 || view >= 31) return 0;
    std::int16_t slot{};
    if (!memory::read(manager, 0x114 + index * 6, slot) || slot < 0 || slot >= 1024) return 0;
    const auto* row = rows + static_cast<std::size_t>(slot) * 0x70;
    std::uint8_t kind{}, owner{}; std::int8_t destination{};
    std::uint32_t token{}, parent{}, views{};
    std::uint16_t global{}, flags{}; const std::byte* aux{}; std::uint64_t stamp{};
    if (!memory::read(row, 0, kind) || kind != 0 || !memory::read(row, 1, owner) || owner != 0xFD
        || !memory::read(row, 8, token) || token == UINT32_MAX || (token & 0x1FFFU) != index
        || lifecycle::resolve(rows, manager, token) != row
        || !memory::read(row, 0xC, parent) || parent != UINT32_MAX
        || !memory::read(row, 0x50, global) || (global & 0x204U)
        || !memory::read(row, 0x60, aux) || !aux
        || !memory::read(aux, 0, destination) || destination != view
        || !memory::read(aux, 4, views) || !(views & (1U << view))) return 0;
    const auto* descriptor = aux + 0x18 + static_cast<std::size_t>(view) * 0x40;
    if (!memory::read(descriptor, 0x38, flags) || (flags & 0x8DU) != 0x88U
        || !memory::read(descriptor, 0x20, stamp) || clock.now < stamp) return 0;
    return clock.now - stamp;
}

inline std::size_t apply(void* lane, std::uint32_t channel, std::uint32_t laneIndex,
                          std::int32_t capacity, std::int32_t count, std::uint32_t* output,
                          memory::Clock clock) noexcept {
    if (!configured.load(std::memory_order_acquire) || !lane || !output || !clock.valid
        || channel > 2 || laneIndex != 2 || capacity < 0 || capacity > 2000 || count <= 0 || count > capacity) return 0;
    struct Candidate { std::int32_t index{-1}; std::uint32_t packed{}; std::uint64_t age{}; };
    std::array<Candidate,kBurst> oldest{};
    constexpr std::uint32_t scanLimit = 256;
    const auto start = scanCursor.fetch_add(scanLimit, std::memory_order_relaxed) % static_cast<std::uint32_t>(count);
    const auto limit = count < static_cast<std::int32_t>(scanLimit) ? count : static_cast<std::int32_t>(scanLimit);
    for (std::int32_t step = 0; step < limit; ++step) {
        const auto i = static_cast<std::int32_t>((start + step) % count);
        std::uint32_t packed{};
        if (!memory::read(output, static_cast<std::size_t>(i) * 4, packed)
            || (packed & memory::kPriorityMask) == memory::kPriorityMask) continue;
        const auto age = eligible_age(lane, packed, clock, channel);
        if (age < kWaitMs) continue;
        Candidate candidate{i, packed, age};
        for (auto& saved : oldest) {
            if (saved.index < 0 || candidate.age > saved.age) {
                const auto previous = saved; saved = candidate; candidate = previous;
                if (candidate.index < 0) break;
            }
        }
    }
    std::size_t promoted{};
    for (const auto& candidate : oldest)
        if (candidate.index >= 0 && combat_feedback_priority::promote(output + candidate.index, candidate.packed)) ++promoted;
    return promoted;
}
}
