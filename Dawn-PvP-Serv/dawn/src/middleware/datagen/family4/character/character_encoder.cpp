#include "character_encoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

#include "../../../../state/unlocks/unlocks_runtime.h"
#include "../../character_record/layout.h"
#include "../instance/layout.h"
#include "../progression/progression_bank_keys.h"
#include "abi.h"
#include "equipment_summary_builder.h"
#include "layout.h"

namespace dawn::middleware::datagen::family4::character {
namespace {

constexpr std::uint16_t kEmptyDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();
constexpr std::byte kSeenMessageByte{0xFF};
constexpr std::uint8_t kNativeTrue = 1;
constexpr std::uint8_t kNativeFalse = 0;

[[nodiscard]] bool valid(const state::CharacterState& state) noexcept {
    return state.soid != 0 && state.race <= state::CharacterRace::exo
           && state.gender <= state::CharacterGender::female
           && state.characterClass <= state::CharacterClass::warlock
           && std::isfinite(state.appearanceValue);
}

constexpr std::size_t kBitsPerFlagByte = 8;
// watermark of an occupied row; empty keeps 0. this is the character object's own per-row field,
// not the item instance's roll progress, so it doesn't share that constant
constexpr std::int32_t kOccupiedRowWatermark = 1;

[[nodiscard]] bool valid(const loadout::ResolvedLoadout& resolvedLoadout) noexcept {
    if (resolvedLoadout.itemCount > resolvedLoadout.items.size()
        || resolvedLoadout.nextInventorySerial < resolvedLoadout.itemCount
        || resolvedLoadout.nextInventorySerial
               > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
        return false;
    }

    std::array<bool, layout::kInventoryCapacity> occupiedRows{};
    std::array<bool, layout::kEquipmentCapacity> occupiedEquipmentSlots{};
    std::array<std::uint64_t, loadout::kItemCapacity> instanceSoids{};
    for (std::size_t index = 0; index < resolvedLoadout.itemCount; ++index) {
        const loadout::ResolvedItem& item = resolvedLoadout.items[index];
        const instance::ResolvedInstance& itemInstance = item.instance;
        const auto priorSoidsEnd = instanceSoids.cbegin() + static_cast<std::ptrdiff_t>(index);
        if (item.inventoryRow >= occupiedRows.size()
            || item.equipmentSlot >= occupiedEquipmentSlots.size() || item.quantity <= 0
            || itemInstance.instanceSoid == 0 || itemInstance.bounds.itemDefinitionCount == 0
            || itemInstance.bounds.itemDefinitionCount > instance::layout::kDefinitionIndexCapacity
            || itemInstance.baseDefinitionIndex == kEmptyDefinitionIndex
            || itemInstance.baseDefinitionIndex >= itemInstance.bounds.itemDefinitionCount
            || item.mutationSerial < 0
            || static_cast<std::uint32_t>(item.mutationSerial)
                   >= resolvedLoadout.nextInventorySerial
            || occupiedRows[item.inventoryRow]
            || (item.equipped && occupiedEquipmentSlots[item.equipmentSlot])
            || std::find(instanceSoids.cbegin(), priorSoidsEnd, itemInstance.instanceSoid)
                   != priorSoidsEnd
            || (index != 0 && resolvedLoadout.items[index - 1].inventoryRow >= item.inventoryRow)) {
            return false;
        }
        occupiedRows[item.inventoryRow] = true;
        if (item.equipped) {
            occupiedEquipmentSlots[item.equipmentSlot] = true;
        }
        instanceSoids[index] = itemInstance.instanceSoid;
    }
    return true;
}

[[nodiscard]] bool
summary_matches_loadout(const loadout::ResolvedLoadout& resolvedLoadout,
                        const state::equipment::light::Evaluation& evaluation) noexcept {
    std::size_t summaryItemCount = 0;
    for (std::size_t index = 0; index < resolvedLoadout.itemCount; ++index) {
        summaryItemCount += static_cast<std::size_t>(resolvedLoadout.items[index].equipped);
    }
    std::size_t scoreCount = 0;
    for (const std::optional<state::equipment::light::ItemScore>& score : evaluation.character) {
        scoreCount += static_cast<std::size_t>(score.has_value());
    }
    if (summaryItemCount != scoreCount) {
        return false;
    }
    for (std::size_t index = 0; index < resolvedLoadout.itemCount; ++index) {
        const loadout::ResolvedItem& item = resolvedLoadout.items[index];
        if (!item.equipped) {
            continue;
        }
        const auto& score = evaluation.character[item.equipmentSlot];
        if (!score.has_value() || score->definitionIndex != item.instance.baseDefinitionIndex) {
            return false;
        }
    }
    return true;
}

} // namespace

bool encode(const state::CharacterState& state,
            const loadout::ResolvedLoadout& resolvedLoadout,
            const state::equipment::light::Evaluation& lightEvaluation,
            std::span<std::byte> output) noexcept {
    if (!valid(state) || !valid(resolvedLoadout)
        || !summary_matches_loadout(resolvedLoadout, lightEvaluation)
        || output.size() < layout::kObjectSize) {
        return false;
    }

    layout::Object object{};
    object.characterSoid = state.soid;
    object.identity.race = static_cast<std::int8_t>(state.race);
    object.identity.gender = static_cast<std::int8_t>(state.gender);
    object.identity.characterClass = static_cast<std::int8_t>(state.characterClass);
    // object B +12 is the same 36-byte identity header (0x80807C12) the family-0 and family-3
    // records carry. it was never written, so the Character tab drew an all-zero face; write the same bytes
    static_assert(layout::kCreationHeaderSize
                  == ::dawn::middleware::datagen::character_record::layout::kHeaderBlockSize);
    std::memcpy(object.creationHeader.data(),
                state.hasIdentityHeader
                    ? state.identityHeader.data()
                    : ::dawn::middleware::datagen::character_record::layout::kHeaderBlockBytes.data(),
                object.creationHeader.size());
    object.lastOrbitedDestination = state.lastOrbitedDestination;
    object.previewMirrors.fill(state.previewAvailable ? kNativeTrue : kNativeFalse);
    object.contentBypass = state.contentBypass ? kNativeTrue : kNativeFalse;
    object.seenMessages.fill(kSeenMessageByte);
    for (inventory::layout::Entry& item : object.inventoryItems) {
        item.definitionIndex = kEmptyDefinitionIndex;
    }
    const state::unlocks::Table& unlocks = state::unlocks::get();
    for (std::size_t index = 0; index < object.acquiredFlags.size(); ++index) {
        object.acquiredFlags[index] = static_cast<std::byte>(
            index < unlocks.characterObjectFlags.size() ? unlocks.characterObjectFlags[index]
                                                        : std::uint8_t{});
    }
    for (std::size_t index = 0; index < object.objectiveValues.size(); ++index) {
        object.objectiveValues[index] =
            index < unlocks.characterObjectValues.size() ? unlocks.characterObjectValues[index] : 0;
    }
    if (!build_equipment_summary(lightEvaluation, object.equipmentSummary)) {
        return false;
    }
    if (!progression::key_bank(state::build_data::progressions::Scope::character,
                               object.progressions)) {
        return false;
    }
    object.nextInventorySerial = resolvedLoadout.nextInventorySerial;
    for (std::size_t index = 0; index < resolvedLoadout.itemCount; ++index) {
        const loadout::ResolvedItem& item = resolvedLoadout.items[index];
        inventory::layout::Entry& inventoryRow = object.inventoryItems[item.inventoryRow];
        inventoryRow.definitionIndex = item.instance.baseDefinitionIndex;
        inventoryRow.instanceSoid = item.instance.instanceSoid;
        inventoryRow.quantity = item.quantity;
        inventoryRow.mutationSerial = item.mutationSerial;
        inventoryRow.flags = item.flags;
        // both companion arrays are indexed by inventory row, not equipment slot; the client's producer marks every row it fills
        object.newItemFlags[item.inventoryRow / kBitsPerFlagByte] |=
            std::byte{1U} << (item.inventoryRow % kBitsPerFlagByte);
        object.instanceProgressWatermarks[item.inventoryRow] = kOccupiedRowWatermark;
        if (item.equipped) {
            object.equippedInstanceSoids[item.equipmentSlot] = item.instance.instanceSoid;
        }
    }

    std::fill(output.begin(), output.end(), std::byte{});
    std::memcpy(output.data(), &object, sizeof object);
    return true;
}

} // namespace dawn::middleware::datagen::family4::character
