#pragma once

#include <span>

#include "../definition.h"

namespace dawn::state::equipment::light::calculation {

[[nodiscard]] bool evaluate(const SlotScores& selectedCharacter,
                            const SlotScores& profileSlotMaxima,
                            std::span<const SlotScores> otherCharacterScores,
                            Evaluation& output) noexcept;

} // namespace dawn::state::equipment::light::calculation
