#include "configured_equipment_light_resolver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "../../../build_data/runtime.h"
#include "../calculation/equipment_light_calculation.h"

namespace dawn::state::equipment::light::resolution {
namespace {

namespace authored = account::inventory;
namespace build_details = build_data::items::details;
namespace build_items = build_data::items;

constexpr std::size_t kOtherCharacterCapacity = kCharacterCapacity - 1U;

using SemanticSlotMap = std::array<std::optional<std::uint8_t>, authored::kEquipmentSlotCount>;
using NativeSlotMap = std::array<std::optional<std::size_t>, build_details::kEquipmentSlotCount>;

[[nodiscard]] std::size_t occupied_rows(const AccountState& account) noexcept {
    return (std::min)(account.characterCount, account.characters.size());
}

[[nodiscard]] bool
resolve_item(const authored::Item& item, std::size_t& nativeSlot, ItemScore& itemScore) noexcept {
    build_items::Definition definition{};
    build_details::Definition detail{};
    if (!build_data::find_item_definition_hash(item.definitionHash, definition)
        || !build_data::find_configured_item_detail(definition.definitionIndex, detail)
        || detail.definitionIndex != definition.definitionIndex || !detail.equipmentSlot.has_value()
        || *detail.equipmentSlot < 0
        || static_cast<std::size_t>(*detail.equipmentSlot) >= build_details::kEquipmentSlotCount) {
        return false;
    }
    nativeSlot = static_cast<std::size_t>(*detail.equipmentSlot);
    std::int32_t power = 0;
    if (!item_power(item.level, power)) {
        return false;
    }
    itemScore = ItemScore{definition.definitionIndex, power};
    return true;
}

[[nodiscard]] bool resolve_character(const CharacterState& character,
                                     SemanticSlotMap& semanticSlots,
                                     NativeSlotMap& nativeSlots,
                                     SlotScores& output) noexcept {
    SlotScores staged{};
    for (std::size_t semanticSlot = 0; semanticSlot < character.equipment.slots.size();
         ++semanticSlot) {
        const std::optional<authored::Item>& item = character.equipment.slots[semanticSlot];
        if (!item.has_value()) {
            continue;
        }

        std::size_t nativeSlot = 0;
        ItemScore score{};
        if (!resolve_item(*item, nativeSlot, score) || staged[nativeSlot].has_value()) {
            return false;
        }
        if (semanticSlots[semanticSlot].has_value() && *semanticSlots[semanticSlot] != nativeSlot) {
            return false;
        }
        if (nativeSlots[nativeSlot].has_value() && *nativeSlots[nativeSlot] != semanticSlot) {
            return false;
        }
        // commit both directions together so later characters cannot alias a native slot
        semanticSlots[semanticSlot] = static_cast<std::uint8_t>(nativeSlot);
        nativeSlots[nativeSlot] = semanticSlot;
        staged[nativeSlot] = score;
    }
    output = staged;
    return true;
}

} // namespace

bool resolve(const AccountState& account,
             std::size_t selectedCharacterIndex,
             Evaluation& output) noexcept {
    const std::size_t count = occupied_rows(account);
    if (selectedCharacterIndex >= count || !account.characters[selectedCharacterIndex].selected) {
        return false;
    }

    std::array<SlotScores, kCharacterCapacity> characterScores{};
    SemanticSlotMap semanticSlots{};
    NativeSlotMap nativeSlots{};
    for (std::size_t characterIndex = 0; characterIndex < count; ++characterIndex) {
        if (!resolve_character(account.characters[characterIndex],
                               semanticSlots,
                               nativeSlots,
                               characterScores[characterIndex])) {
            return false;
        }
    }

    std::array<SlotScores, kOtherCharacterCapacity> otherCharacters{};
    std::size_t otherCharacterCount = 0;
    for (std::size_t characterIndex = 0; characterIndex < count; ++characterIndex) {
        if (characterIndex != selectedCharacterIndex
            && otherCharacterCount < otherCharacters.size()) {
            otherCharacters[otherCharacterCount++] = characterScores[characterIndex];
        }
    }

    // profile summary array reports the same items as the character array; the generator
    // fills both from one loadout
    const SlotScores& profileSlotMaxima = characterScores[selectedCharacterIndex];
    Evaluation staged{};
    if (!calculation::evaluate(characterScores[selectedCharacterIndex],
                               profileSlotMaxima,
                               std::span(otherCharacters).first(otherCharacterCount),
                               staged)) {
        return false;
    }
    output = staged;
    return true;
}

bool character_light(const AccountState& account,
                     std::size_t characterIndex,
                     std::int32_t& light) noexcept {
    light = 0;
    if (characterIndex >= occupied_rows(account)) {
        return false;
    }
    SlotScores scores{};
    SemanticSlotMap semanticSlots{};
    NativeSlotMap nativeSlots{};
    if (!resolve_character(
            account.characters[characterIndex], semanticSlots, nativeSlots, scores)) {
        return false;
    }
    Evaluation evaluation{};
    if (!calculation::evaluate(scores, SlotScores{}, std::span<const SlotScores>{}, evaluation)) {
        return false;
    }
    light = evaluation.average;
    return true;
}

} // namespace dawn::state::equipment::light::resolution
