#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../build_data/runtime.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"
#include "storage/internal.h"

namespace dawn::state {

using namespace runtime::detail;
namespace authored_inventory = account::inventory;
namespace family4_loadout = middleware::datagen::family4::loadout;

namespace {

constexpr std::size_t kSubclassSlot =
    static_cast<std::size_t>(authored_inventory::EquipmentSlot::subclass);

// entries 2-3 class ability, 4-6 movement, 7-9 grenade. tree 1 = super 10 + melee 11,
// tree 2 = super 10 + melee 15, tree 3 = super 20 + melee 21; any node selects the tree
[[nodiscard]] bool apply_entry(std::uint8_t entry, CharacterState& character) noexcept {
    if (entry == 2 || entry == 3) {
        character.classAbilityEntry = entry;
    } else if (entry >= 4 && entry <= 6) {
        character.movementAbilityEntry = entry;
    } else if (entry >= 7 && entry <= 9) {
        character.grenadeAbilityEntry = entry;
    } else if (entry >= 10 && entry <= 14) {
        character.superAbilityEntry = 10;
        character.meleeAbilityEntry = 11;
    } else if (entry >= 15 && entry <= 18) {
        character.superAbilityEntry = 10;
        character.meleeAbilityEntry = 15;
    } else if (entry >= 20 && entry <= 23) {
        character.superAbilityEntry = 20;
        character.meleeAbilityEntry = 21;
    } else {
        return false;
    }
    return true;
}

void report_ability(std::string_view stage,
                    std::string_view result,
                    std::string_view reason,
                    std::uint64_t characterSoid,
                    std::uint64_t instanceSoid,
                    std::uint8_t entry,
                    const CharacterState* after) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=ability stage=%.*s result=%.*s reason=%.*s character=0x%llX instance=0x%llX entry=%u "
        "movement=%u grenade=%u super=%u melee=%u class=%u",
        static_cast<int>(stage.size()),
        stage.data(),
        static_cast<int>(result.size()),
        result.data(),
        static_cast<int>(reason.size()),
        reason.data(),
        static_cast<unsigned long long>(characterSoid),
        static_cast<unsigned long long>(instanceSoid),
        static_cast<unsigned>(entry),
        after == nullptr ? 0U : after->movementAbilityEntry,
        after == nullptr ? 0U : after->grenadeAbilityEntry,
        after == nullptr ? 0U : after->superAbilityEntry,
        after == nullptr ? 0U : after->meleeAbilityEntry,
        after == nullptr ? 0U : after->classAbilityEntry);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         result == "ok" ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

[[nodiscard]] bool stage_ability_selection(const AccountState& snapshot,
                                           std::size_t characterIndex,
                                           std::uint64_t subclassInstanceSoid,
                                           std::uint8_t socketEntry,
                                           PendingAbilitySelection& mutation,
                                           const char*& reason) noexcept {
    mutation = {};
    reason = "character";
    if (characterIndex >= snapshot.characterCount) {
        return false;
    }
    const CharacterState& before = snapshot.characters[characterIndex];
    const auto& subclass = before.equipment.slots[kSubclassSlot];
    // the entries belong to the character, so only the equipped subclass may change them
    reason = "not_equipped";
    if (!subclass.has_value() || subclass->instanceSoid != subclassInstanceSoid) {
        return false;
    }
    CharacterState after = before;
    reason = "entry";
    if (!apply_entry(socketEntry, after)) {
        return false;
    }
    reason = "unchanged";
    if (same_character(before, after)) {
        return false;
    }

    // without an ability row the character cannot be encoded, and that would fail after the
    // reply promised the update, so refuse the pick up front
    build_data::items::Definition item{};
    build_data::items::details::Definition detail{};
    build_data::abilities::Definition row{};
    reason = "ability_row";
    if (!build_data::find_item_definition_hash(subclass->definitionHash, item)
        || !build_data::find_configured_item_detail(item.definitionIndex, detail)) {
        return false;
    }
    const auto has_row = [&]() noexcept {
        return build_data::find_ability_buckets(detail.socketEntryListIndex,
                                                {after.movementAbilityEntry,
                                                 after.grenadeAbilityEntry,
                                                 after.superAbilityEntry,
                                                 after.meleeAbilityEntry,
                                                 after.classAbilityEntry},
                                                row);
    };
    // Arcstrider's and Sentinel's bottom tree has no super of its own, so it keeps entry 10
    if (!has_row() && after.superAbilityEntry == 20) {
        after.superAbilityEntry = 10;
    }
    if (!has_row()) {
        return false;
    }

    // per-thread: an account image is too large for the BAP worker's stack
    thread_local AccountState candidate;
    candidate = snapshot;
    candidate.characters[characterIndex] = after;
    family4_loadout::ResolvedLoadout checked{};
    reason = "resolve";
    if (!account::valid(candidate)
        || !family4_loadout::resolve(candidate, characterIndex, checked)) {
        return false;
    }

    mutation.beforeCharacter = before;
    mutation.afterCharacter = after;
    mutation.characterSoid = before.soid;
    mutation.subclassInstanceSoid = subclassInstanceSoid;
    mutation.characterIndex = characterIndex;
    mutation.socketEntry = socketEntry;
    mutation.prepared = true;
    reason = "ready";
    return true;
}

} // namespace

bool prepare_ability_selection(std::uint64_t subclassInstanceSoid,
                               std::uint8_t socketEntry,
                               PendingAbilitySelection& mutation) noexcept {
    mutation = {};
    const AccountState snapshot = account_snapshot();
    std::size_t characterIndex = snapshot.characterCount;
    if (account::valid(snapshot)) {
        for (std::size_t index = 0; index < snapshot.characterCount; ++index) {
            if (snapshot.characters[index].selected) {
                characterIndex = index;
                break;
            }
        }
    }
    const char* reason = "selection";
    if (characterIndex >= snapshot.characterCount
        || !stage_ability_selection(
            snapshot, characterIndex, subclassInstanceSoid, socketEntry, mutation, reason)) {
        report_ability("prepare",
                       "fail",
                       reason,
                       characterIndex < snapshot.characterCount
                           ? snapshot.characters[characterIndex].soid
                           : 0,
                       subclassInstanceSoid,
                       socketEntry,
                       nullptr);
        mutation = {};
        return false;
    }
    report_ability("prepare",
                   "ok",
                   reason,
                   mutation.characterSoid,
                   subclassInstanceSoid,
                   socketEntry,
                   &mutation.afterCharacter);
    return true;
}

bool commit_ability_selection(PendingAbilitySelection& mutation) noexcept {
    thread_local PendingAbilitySelection prepared;
    prepared = mutation;
    mutation = {};
    const auto fail = [](std::string_view reason) noexcept {
        report_ability("commit",
                       "fail",
                       reason,
                       prepared.characterSoid,
                       prepared.subclassInstanceSoid,
                       prepared.socketEntry,
                       nullptr);
        return false;
    };
    if (!prepared.prepared || prepared.characterSoid == 0
        || prepared.characterIndex >= kCharacterCapacity
        || prepared.beforeCharacter.soid != prepared.characterSoid
        || prepared.afterCharacter.soid != prepared.characterSoid) {
        return fail("mutation");
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    thread_local AccountState candidate;
    candidate = runtime::storage::account();
    if (prepared.characterIndex >= candidate.characterCount
        || !same_character(candidate.characters[prepared.characterIndex],
                           prepared.beforeCharacter)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return fail("stale");
    }
    thread_local PendingAbilitySelection canonical;
    const char* reason = "";
    if (!stage_ability_selection(candidate,
                                 prepared.characterIndex,
                                 prepared.subclassInstanceSoid,
                                 prepared.socketEntry,
                                 canonical,
                                 reason)
        || !same_character(canonical.afterCharacter, prepared.afterCharacter)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return fail("transition");
    }
    candidate.characters[prepared.characterIndex] = prepared.afterCharacter;
    if (!runtime::storage::publish_account_locked(candidate)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return fail("publish");
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    report_ability("commit",
                   "ok",
                   "published",
                   prepared.characterSoid,
                   prepared.subclassInstanceSoid,
                   prepared.socketEntry,
                   &prepared.afterCharacter);
    return true;
}

} // namespace dawn::state
