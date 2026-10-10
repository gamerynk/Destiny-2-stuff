// an item's socket entries start absent or ready; entries the character selected make their
// whole group active, and the super lane is active although it has no plug source

#include "subclass_socket_selection.h"

#include "../../../../state/build_data/runtime.h"

namespace dawn::middleware::datagen::family4::loadout {
namespace {

namespace build_socket_lists = state::build_data::socket_entry_lists;

// bucket the grenade publishes into. entry order is sprint, class, movement, grenade, super, melee
constexpr std::uint8_t kGrenadeBucket = 0;
constexpr std::uint8_t kSuperBucket = 1;
constexpr std::uint8_t kMeleeBucket = 2;
constexpr std::uint8_t kMovementBucket = 3;
// sprint isn't selectable, so its entry is fixed
constexpr std::uint8_t kSprintBucket = 4;
constexpr std::uint8_t kSprintEntry = 1;

[[nodiscard]] std::uint8_t class_ability_bucket(state::CharacterClass characterClass) noexcept {
    switch (characterClass) {
    case state::CharacterClass::hunter:
        return 9;
    case state::CharacterClass::warlock:
        return 11;
    case state::CharacterClass::titan:
    default:
        return 6;
    }
}

} // namespace

void subclass_selection(const state::CharacterState& character,
                        SubclassSelection& output) noexcept {
    output = {};
    output.selected[0] = {character.grenadeAbilityEntry, kGrenadeBucket};
    // melee claims its group before super: the bottom tree's super shares the melee's group, and
    // claiming it first left the melee and tree perks inactive
    output.selected[1] = {character.meleeAbilityEntry, kMeleeBucket};
    output.selected[2] = {character.superAbilityEntry, kSuperBucket};
    output.selected[3] = {character.movementAbilityEntry, kMovementBucket};
    output.selected[4] = {kSprintEntry, kSprintBucket};
    output.selected[5] = {character.classAbilityEntry,
                          class_ability_bucket(character.characterClass)};
}

void resolve_socket_states(
    const build_socket_lists::Definition& definition,
    const state::CharacterState& character,
    std::array<instance::SocketEntryState, instance::layout::kSocketEntryStateCapacity>& output,
    std::array<instance::SocketSelector, kSelectorBucketCount>& selectors) noexcept {
    output.fill(instance::SocketEntryState::absent);
    selectors.fill(instance::SocketSelector{});
    for (std::size_t index = 0; index < definition.entryCount; ++index) {
        const std::uint64_t bit = std::uint64_t{1} << index;
        if ((definition.readyMask & bit) != 0) {
            output[index] = instance::SocketEntryState::ready;
        }
    }
    // only a subclass keeps an entry table, so this lookup is what identifies one
    build_socket_lists::EntryTable entries{};
    if (!state::build_data::find_socket_entry_table(definition.definitionIndex, entries)) {
        return;
    }
    SubclassSelection selection{};
    subclass_selection(character, selection);

    // each selected entry claims its group; every entry sharing that group and plug source is
    // active too, so a run of duplicate lanes flips together
    std::array<std::uint32_t, build_socket_lists::kEntryCapacity> chosen{};
    std::array<bool, build_socket_lists::kEntryCapacity> claimed{};
    for (const SelectedEntry& selected : selection.selected) {
        if (selected.entry >= definition.entryCount || selected.bucket >= selectors.size()) {
            continue;
        }
        selectors[selected.bucket] = instance::SocketSelector{selected.entry, 0, 0};
        const build_socket_lists::Entry& entry = entries.entries[selected.entry];
        if (entry.plugSource == build_socket_lists::kNoPlugSource || entry.group >= claimed.size()
            || claimed[entry.group]) {
            continue;
        }
        claimed[entry.group] = true;
        chosen[entry.group] = entry.plugSource;
    }
    for (std::size_t index = 0; index < definition.entryCount; ++index) {
        const build_socket_lists::Entry& entry = entries.entries[index];
        const bool matchesGroup = entry.plugSource != build_socket_lists::kNoPlugSource
                                  && entry.group < claimed.size() && claimed[entry.group]
                                  && chosen[entry.group] == entry.plugSource;
        const bool superLane = entry.plugSource == build_socket_lists::kNoPlugSource
                               && entry.kind == build_socket_lists::kSuperEntryKind;
        if (matchesGroup || superLane) {
            output[index] = instance::SocketEntryState::active;
        }
    }
}

} // namespace dawn::middleware::datagen::family4::loadout
