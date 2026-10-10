#include "equipment_light_calculation.h"

#include <limits>

namespace dawn::state::equipment::light::calculation {
namespace {

void merge_strict_upgrades(const SlotScores& selectedCharacter,
                           const SlotScores& candidate,
                           SlotScores& aggregate) noexcept {
    for (std::size_t slotIndex = 0; slotIndex < aggregate.size(); ++slotIndex) {
        // candidates cannot create a slot absent from the selected character
        if (!selectedCharacter[slotIndex].has_value() || !candidate[slotIndex].has_value()) {
            continue;
        }

        // strict compare keeps the selected item winning ties
        if (candidate[slotIndex]->score > aggregate[slotIndex]->score) {
            aggregate[slotIndex] = candidate[slotIndex];
        }
    }
}

[[nodiscard]] SlotScores
merge_aggregate(const SlotScores& selectedCharacter,
                const SlotScores& profileSlotMaxima,
                const std::span<const SlotScores> otherCharacterScores) noexcept {
    SlotScores aggregate = selectedCharacter;
    merge_strict_upgrades(selectedCharacter, profileSlotMaxima, aggregate);
    for (const SlotScores& otherCharacter : otherCharacterScores) {
        merge_strict_upgrades(selectedCharacter, otherCharacter, aggregate);
    }
    return aggregate;
}

void add_slot_score(const SlotScores& scores,
                    const std::size_t slotIndex,
                    std::int64_t& total) noexcept {
    if (scores[slotIndex].has_value()) {
        total += scores[slotIndex]->score;
    }
}

// native producer totals all 20 slots, so an unpowered slot adds its zero
[[nodiscard]] std::int64_t slot_total(const SlotScores& aggregate) noexcept {
    std::int64_t total = 0;
    for (std::size_t slotIndex = 0; slotIndex < aggregate.size(); ++slotIndex) {
        add_slot_score(aggregate, slotIndex, total);
    }
    return total;
}

[[nodiscard]] bool fits_summary_integer(const std::int64_t value) noexcept {
    return value >= std::numeric_limits<std::int32_t>::min()
           && value <= std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] std::int32_t divisor_for(const SlotScores& aggregate) noexcept {
    std::int32_t counted = 0;
    for (const std::optional<ItemScore>& score : aggregate) {
        if (score.has_value() && score->score > kUnpoweredScore) {
            ++counted;
        }
    }
    return counted;
}

} // namespace

bool evaluate(const SlotScores& selectedCharacter,
              const SlotScores& profileSlotMaxima,
              std::span<const SlotScores> otherCharacterScores,
              Evaluation& output) noexcept {
    const SlotScores aggregate =
        merge_aggregate(selectedCharacter, profileSlotMaxima, otherCharacterScores);
    const std::int64_t wideTotal = slot_total(aggregate);
    if (!fits_summary_integer(wideTotal)) {
        return false;
    }

    Evaluation candidate;
    candidate.profile = profileSlotMaxima;
    candidate.character = selectedCharacter;
    candidate.divisor = divisor_for(aggregate);
    candidate.total = static_cast<std::int32_t>(wideTotal);
    // nothing powered means no average; dividing would trap
    candidate.average = candidate.divisor == 0 ? 0 : candidate.total / candidate.divisor;
    candidate.averageFloat = candidate.divisor == 0 ? 0.0F
                                                    : static_cast<float>(candidate.total)
                                                          / static_cast<float>(candidate.divisor);
    output = candidate;
    return true;
}

} // namespace dawn::state::equipment::light::calculation
