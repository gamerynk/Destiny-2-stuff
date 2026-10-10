#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::datagen::family4::progression::layout {

inline constexpr std::size_t kValueLaneCount = 3;

// lane 0 is the progress the level walk reads
using Values = std::array<std::int32_t, kValueLaneCount>;

#pragma pack(push, 1)

struct Entry {
    Values values{};
    std::uint16_t definitionIndex{};
    std::uint16_t reserved{};
};

#pragma pack(pop)

static_assert(sizeof(Entry) == sizeof(Values) + 2 * sizeof(std::uint16_t));

} // namespace dawn::middleware::datagen::family4::progression::layout
