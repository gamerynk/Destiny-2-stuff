#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "../../build_data/items/details/definition.h"

namespace dawn::state::equipment::light {

struct ItemScore {
    std::uint16_t definitionIndex{};
    std::int32_t score{};

    [[nodiscard]] bool operator==(const ItemScore&) const noexcept = default;
};

using SlotScores =
    std::array<std::optional<ItemScore>, build_data::items::details::kEquipmentSlotCount>;

inline constexpr std::int32_t kPowerPerLevel = 10;
// a powered item never scores below this, whatever its level
inline constexpr std::int32_t kMinimumItemPower = 750;

[[nodiscard]] constexpr bool item_power(std::int32_t level, std::int32_t& output) noexcept {
    if (level < 0 || level > (std::numeric_limits<std::int32_t>::max)() / kPowerPerLevel) {
        return false;
    }
    const std::int32_t power = kPowerPerLevel * level;
    output = level == 0 ? 0 : (power < kMinimumItemPower ? kMinimumItemPower : power);
    return true;
}

// a slot at or below this adds nothing and is not counted by the divisor
inline constexpr std::int32_t kUnpoweredScore = 0;

struct Evaluation {
    SlotScores profile{};
    SlotScores character{};
    std::int32_t divisor{};
    std::int32_t total{};
    std::int32_t average{};
    float averageFloat{};

    [[nodiscard]] bool operator==(const Evaluation&) const noexcept = default;
};

struct ScalarValue {
    std::int32_t average{};
    float value{};

    [[nodiscard]] bool operator==(const ScalarValue&) const noexcept = default;
};

} // namespace dawn::state::equipment::light
