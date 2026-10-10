#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pvp_native_query_component {
struct Snapshot {
    std::array<std::byte, 24> descriptor{};
    std::uintptr_t callback{}, header{}, tables{}, records{}, slot{}, component{};
    std::uint64_t relocation{};
    std::uint32_t stride{}, mask{}, type{}, legacy{};
    bool operator==(const Snapshot&) const = default;
};
inline bool address(std::uintptr_t value, std::size_t count = 1) noexcept {
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFFFULL
        && count <= 0x00007FFFFFFFFFFFULL - value;
}
template<class Read> bool resolve(std::uintptr_t image, std::uintptr_t query,
    std::uintptr_t actualCallback, Read&& read, Snapshot& output) noexcept {
    Snapshot candidate{};
    const auto add = [](std::uintptr_t base, std::uint64_t offset, std::uintptr_t& result) {
        if (!address(base) || offset > 0x00007FFFFFFFFFFFULL - base) return false;
        result = base + static_cast<std::uintptr_t>(offset);
        return true;
    };
    auto load = [&read](std::uintptr_t where, auto& value) {
        return address(where, sizeof value) && read(where, &value, sizeof value);
    };
    std::uintptr_t descriptor{}, cell{};
    if (!add(query, 0x30, descriptor) || !add(image, 0xE8A870, cell)
        || actualCallback != cell || !load(descriptor, candidate.descriptor)) return false;
    auto descriptor32 = [&candidate](std::size_t offset) {
        std::uint32_t value{};
        for (unsigned i = 0; i < 4; ++i)
            value |= std::to_integer<std::uint32_t>(candidate.descriptor[offset + i]) << (i * 8);
        return value;
    };
    std::uint64_t offset{};
    for (unsigned i = 0; i < 8; ++i)
        offset |= std::to_integer<std::uint64_t>(candidate.descriptor[16 + i]) << (i * 8);
    const auto method = descriptor32(0), datum = descriptor32(8);
    if (!method || method == UINT32_MAX || datum == UINT32_MAX) return false;
    candidate.callback = actualCallback;
    if (!add(image, 0x2439C70, cell) || !load(cell, candidate.header)
        || !load(candidate.header, candidate.tables)) return false;
    const auto shifted = static_cast<std::uint32_t>(static_cast<std::int32_t>(datum) >> 13);
    const auto bank = ((shifted | 0x0FFC0000U) >> 18) & (shifted & 0xFFFFU);
    std::uintptr_t table{};
    if (bank > 0x3FFF || !add(candidate.tables, static_cast<std::uint64_t>(bank) * 0x40, table)
        || !add(table, 8, cell) || !load(cell, candidate.records)
        || !add(table, 0x30, cell) || !load(cell, candidate.stride)
        || candidate.stride < 16 || candidate.stride > 0x100000
        || !add(table, 0x34, cell) || !load(cell, candidate.mask)
        || !add(candidate.records, static_cast<std::uint64_t>(datum & 0x1FFFU) * candidate.stride, candidate.slot)
        || !add(candidate.slot, 8, cell) || !load(cell, candidate.relocation)) return false;
    const auto signedMask = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(candidate.mask)));
    const auto correction = signedMask & candidate.relocation;
    // native 18536B8..C1 computes raw + offset - correction; reject arithmetic wrap rather than
    // trust a wrapped readable pointer
    std::uintptr_t adjusted{};
    if (!add(candidate.slot, offset, adjusted) || correction > adjusted) return false;
    candidate.component = adjusted - static_cast<std::uintptr_t>(correction);
    if (!add(candidate.component, 4, cell) || !load(cell, candidate.type)
        || candidate.type != 0x80804494U || !add(candidate.component, 0x2C, cell)
        || !load(cell, candidate.legacy) || candidate.legacy == UINT32_MAX) return false;
    std::array<std::byte, 24> again{};
    if (!load(descriptor, again) || again != candidate.descriptor) return false;
    output = candidate;
    return true;
}
template<class Read> bool unchanged(std::uintptr_t image, std::uintptr_t query,
    std::uintptr_t actualCallback, Read&& read, const Snapshot& before) noexcept {
    Snapshot after{};
    return resolve(image, query, actualCallback, read, after) && after == before;
}
} // namespace pvp_native_query_component
