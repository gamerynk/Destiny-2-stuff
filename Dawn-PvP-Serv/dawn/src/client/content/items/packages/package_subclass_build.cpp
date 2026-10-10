#include <array>

#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/runtime/runtime.h"
#include "internal.h"

namespace dawn::client::content::items::packages {
namespace {

namespace domain = state::build_data::abilities;

constexpr std::size_t kSubclassSlot =
    static_cast<std::size_t>(state::account::inventory::EquipmentSlot::subclass);

[[nodiscard]] bool subclass_list(const state::CharacterState& character,
                                 std::uint16_t& socketEntryListIndex,
                                 const char*& reason) noexcept {
    const auto& slot = character.equipment.slots[kSubclassSlot];
    state::build_data::items::Definition item{};
    state::build_data::items::details::Definition detail{};
    reason = "subclass_slot";
    if (!slot.has_value()) {
        return false;
    }
    reason = "subclass_item";
    if (!state::build_data::find_item_definition_hash(slot->definitionHash, item)) {
        return false;
    }
    reason = "subclass_detail";
    if (!state::build_data::find_configured_item_detail(item.definitionIndex, detail)) {
        return false;
    }
    socketEntryListIndex = detail.socketEntryListIndex;
    return true;
}

[[nodiscard]] bool held(std::span<const domain::Definition> rows,
                        const domain::Definition& row) noexcept {
    for (const domain::Definition& existing : rows) {
        if (existing.socketEntryListIndex == row.socketEntryListIndex
            && existing.selection == row.selection) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] domain::Selection selection_of(const state::CharacterState& character) noexcept {
    return {character.movementAbilityEntry,
            character.grenadeAbilityEntry,
            character.superAbilityEntry,
            character.meleeAbilityEntry,
            character.classAbilityEntry};
}

} // namespace

bool build_character_abilities(const reader::Source& source,
                               reader::Scratch& scratch,
                               std::span<const std::byte> root,
                               std::vector<std::byte>& table,
                               std::vector<std::byte>& definition,
                               std::vector<std::byte>& blob,
                               std::span<state::build_data::abilities::Definition> output,
                               std::size_t& count) noexcept {
    count = 0;
    std::uint32_t tableTag = 0;
    tables::Array rows{};
    if (!tables::slot_tag(root, tables::kSocketEntryListTableSlot, tableTag) || tableTag == 0) {
        report_ability_failure("table_slot", 0, root.size(), tableTag);
        return false;
    }
    if (!reader::read_tag(source, scratch, tableTag, table)) {
        report_ability_failure("table_read", 0, tableTag, 0);
        return false;
    }
    if (!tables::find_array_at(
            std::span<const std::byte>{table}, tables::kTableArrayDescriptor, rows)) {
        report_ability_failure("table_array", 0, table.size(), 0);
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    for (std::size_t character = 0; character < account.characterCount && count < output.size();
         ++character) {
        domain::Definition row{};
        const char* subclassReason = "subclass";
        if (!subclass_list(
                account.characters[character], row.socketEntryListIndex, subclassReason)) {
            const auto& subclass = account.characters[character].equipment.slots[kSubclassSlot];
            report_ability_failure(
                subclassReason, character, subclass.has_value() ? subclass->definitionHash : 0, 0);
            continue;
        }
        const domain::Selection selection = selection_of(account.characters[character]);
        row.selection = selection;
        if (held(output.first(count), row)) {
            continue;
        }
        tables::IndexRow indexRow{};
        if (!tables::index_row(
                std::span<const std::byte>{table}, rows, row.socketEntryListIndex, indexRow)
            || indexRow.targetTag == 0) {
            report_ability_failure("index_row", character, row.socketEntryListIndex, rows.count);
            continue;
        }
        if (!reader::read_tag(source, scratch, indexRow.targetTag, definition)) {
            report_ability_failure(
                "definition_read", character, row.socketEntryListIndex, indexRow.targetTag);
            continue;
        }
        if (!build_ability_buckets(
                source, scratch, std::span<const std::byte>{definition}, blob, selection, row)) {
            const std::size_t packedSelection =
                selection.movementEntry | (selection.grenadeEntry << 8U)
                | (selection.superEntry << 16U) | (selection.meleeEntry << 24U);
            report_ability_failure(
                "bucket_build", character, row.socketEntryListIndex, packedSelection);
            continue;
        }
        output[count++] = row;
    }

    // every subclass's whole tree and ability space, so another subclass always finds its row.
    // lists 1-3 are Hunter, 5-7 Titan, 9-11 Warlock; entry meanings are the same on all nine
    constexpr std::array<std::uint16_t, 9> kSubclassLists{1, 2, 3, 5, 6, 7, 9, 10, 11};
    constexpr std::array<std::uint8_t, 3> kMovementEntries{4, 5, 6};
    constexpr std::array<std::uint8_t, 3> kGrenadeEntries{7, 8, 9};
    constexpr std::array<std::array<std::uint8_t, 2>, 3> kTrees{{{10, 11}, {10, 15}, {20, 21}}};
    constexpr std::array<std::uint8_t, 2> kClassEntries{2, 3};
    for (const std::uint16_t list : kSubclassLists) {
        tables::IndexRow indexRow{};
        if (!tables::index_row(std::span<const std::byte>{table}, rows, list, indexRow)
            || indexRow.targetTag == 0
            || !reader::read_tag(source, scratch, indexRow.targetTag, definition)) {
            report_ability_failure("combination_list", 0, list, rows.count);
            continue;
        }
        for (const std::uint8_t movement : kMovementEntries) {
            for (const std::uint8_t grenade : kGrenadeEntries) {
                for (const auto& tree : kTrees) {
                    for (const std::uint8_t classEntry : kClassEntries) {
                        if (count >= output.size()) {
                            report_ability_failure("combination_capacity", count, list, output.size());
                            return true;
                        }
                        domain::Definition row{};
                        row.socketEntryListIndex = list;
                        row.selection = {movement, grenade, tree[0], tree[1], classEntry};
                        if (held(output.first(count), row)) {
                            continue;
                        }
                        const domain::Selection selection = row.selection;
                        if (!build_ability_buckets(source,
                                                   scratch,
                                                   std::span<const std::byte>{definition},
                                                   blob,
                                                   selection,
                                                   row)) {
                            // Arcstrider and Sentinel keep entry 10 on their bottom tree; cache that selection too, as runtime falls back to it
                            if (selection.superEntry != 20) continue;
                            row.selection.superEntry = 10;
                            if (held(output.first(count), row)) continue;
                            if (!build_ability_buckets(source, scratch,
                                    std::span<const std::byte>{definition}, blob,
                                    row.selection, row)) continue;
                        }
                        output[count++] = row;
                    }
                }
            }
        }
    }
    if (count == 0) {
        report_ability_failure("empty", account.characterCount, rows.count, output.size());
    }
    return true;
}

} // namespace dawn::client::content::items::packages
