#pragma once

#include <cstdint>

namespace dawn::state::build_data {

struct BuildIdentity {
    std::uint32_t imageTimestamp{};
    std::uint32_t imageSize{};
    // authored item levels and light-stat rules, so a mismatched cache is not reused
    std::uint64_t configuredEquipmentHash{};

    [[nodiscard]] constexpr bool operator==(const BuildIdentity& other) const noexcept = default;
};

} // namespace dawn::state::build_data
