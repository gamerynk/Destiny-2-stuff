#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::squad_sense {

// root schema for a slot-type-1 squad sensor and its nested arrays: 80807ECF holds the
// consumed-request counts, 80807ECD the task evaluator's own costs
inline constexpr std::uint32_t kSchema = 0x80807ECCU, kCountSchema = 0x80807ECFU,
                               kCostSchema = 0x80807ECDU;
inline constexpr std::size_t kTaskGroupCount = 24;
inline constexpr std::size_t kCountCapacity = 8;
// costs are 7 bits against a native maximum of 2040; the top code means "cannot be reached
// from here", not an expensive route, so a group reporting it must never be selected
inline constexpr std::uint8_t kUnreachableCost = 127;
inline constexpr std::size_t kRevisionScalar = 1, kAliveScalar = 3;
inline constexpr std::size_t kScalarCount = 6;

// absent optional fields keep their published defaults, so merge this against what
// the caller already holds instead of treating it as a whole state
struct Output final {
    std::array<std::uint8_t, kTaskGroupCount> cost{};
    std::uint32_t costMask{};
    std::array<std::int32_t, kCountCapacity> counts{};
    std::uint32_t revision{};
    std::uint32_t alive{};
    std::uint8_t countLength{};
    bool hasRevision{}, hasAlive{}, initialized{}, removal{}, costsPresent{}, countsPresent{};
};

template<class Reader>
[[nodiscard]] bool optional_scalar(Reader& reader, std::size_t width, std::uint64_t& value,
                                   bool& present) noexcept {
    std::uint64_t flag{};
    if (!reader.read(1, flag)) { return false; }
    present = flag != 0;
    return !present || reader.read(static_cast<std::uint8_t>(width), value);
}

// caller already consumed the root presence bit and owns the raw revision that follows;
// bit consumption matches the reflected schema, so it can stand in for a skip
template<class Reader>
[[nodiscard]] bool read_delta(Reader& reader, Output& output) noexcept {
    static constexpr std::size_t kScalarWidths[kScalarCount]{31, 31, 31, 6, 7, 31};
    Output result{};
    for (std::size_t index = 0; index < kScalarCount; ++index) {
        std::uint64_t value{};
        bool present{};
        if (!optional_scalar(reader, kScalarWidths[index], value, present)) { return false; }
        if (!present) { continue; }
        if (index == kRevisionScalar) {
            result.revision = static_cast<std::uint32_t>(value);
            result.hasRevision = true;
        } else if (index == kAliveScalar) {
            result.alive = static_cast<std::uint32_t>(value);
            result.hasAlive = true;
        }
    }
    // fields 6..10 are required, so no presence bit: two enum bits, three more, then the
    // member-removal flag, one unused flag and the initialized latch
    std::uint64_t required{};
    if (!reader.read(2, required) || !reader.read(3, required)) { return false; }
    if (!reader.read(1, required)) { return false; }
    result.removal = required != 0;
    if (!reader.read(1, required) || !reader.read(1, required)) { return false; }
    result.initialized = required != 0;

    std::uint64_t present{};
    if (!reader.read(1, present)) { return false; }
    if (present != 0) {
        std::uint64_t count{};
        if (!reader.read(4, count) || count > kCountCapacity) { return false; }
        result.countsPresent = true;
        result.countLength = static_cast<std::uint8_t>(count);
        for (std::uint64_t index = 0; index < count; ++index) {
            std::uint64_t value{};
            if (!reader.read(32, value)) { return false; }
            result.counts[index] = static_cast<std::int32_t>(
                static_cast<std::int64_t>(value) - static_cast<std::int64_t>(0x80000000LL));
        }
    }
    if (!reader.read(1, present)) { return false; }
    if (present != 0) {
        result.costsPresent = true;
        for (std::size_t index = 0; index < kTaskGroupCount; ++index) {
            std::uint64_t value{};
            bool sent{};
            if (!optional_scalar(reader, 7, value, sent)) { return false; }
            if (!sent) { continue; }
            result.cost[index] = static_cast<std::uint8_t>(value);
            result.costMask |= std::uint32_t{1} << index;
        }
    }
    output = result;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::squad_sense
