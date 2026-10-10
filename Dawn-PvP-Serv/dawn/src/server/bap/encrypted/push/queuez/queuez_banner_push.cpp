#include <array>
#include <cstdio>
#include <limits>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../../../state/account/account_state.h"
#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../queuez/queuez_state_validation.h"
#include "../snapshot/snapshot.h"
#include "queuez_push_reporting.h"
#include "queuez_update_frame.h"

namespace dawn::server::bap::encrypted::push {
namespace {

constexpr std::size_t kSkipLineCapacity = 96;

void report_skip(const char* reason) noexcept {
    std::array<char, kSkipLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=queuez stage=banner_move result=skip reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

void report_fail(const char* stage, const char* reason) noexcept {
    std::array<char, kSkipLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=queuez stage=%s result=fail reason=%s", stage, reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool append_appearance_frame(Scratch& scratch,
                                           const queuez::CharacterAppearanceRefresh& refresh,
                                           snapshot::Prepared& prepared,
                                           const char* stage,
                                           std::span<const std::byte, state::kAesKeySize> key,
                                           std::array<std::byte, state::kBapNonceSize>& nonce,
                                           std::span<std::byte> response,
                                           std::size_t& written) noexcept {
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t beforeBytes = written;
    const bool replacement =
        objectCount >= 2
        && prepared.family.objects.front().id == middleware::datagen::kBannerCharacterObjectId
        && prepared.family.objects.front().version == refresh.characterSoid
        && prepared.family.objects.front().payload.empty();
    const std::size_t characterIndex = replacement ? 1U : 0U;
    const bool hasAnchor = objectCount == characterIndex + 2U;
    if ((objectCount != characterIndex + 1U && !hasAnchor)
        || prepared.family.type != queuez::kBannerFamilyType
        || prepared.family.rootSoid != refresh.after.family4RootSoid
        || prepared.family.version != refresh.after.family0Version || prepared.family.flags != 0
        || (replacement
            && prepared.family.objects.front().encoding != middleware::queuez::Encoding::raw)
        || prepared.family.objects[characterIndex].id
               != middleware::datagen::kBannerCharacterObjectId
        || prepared.family.objects[characterIndex].version != refresh.characterSoid
        || prepared.family.objects[characterIndex].encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects[characterIndex].payload.empty()
        || (hasAnchor
            && (prepared.family.objects.back().id != middleware::datagen::kBannerAnchorObjectId
                || prepared.family.objects.back().version != refresh.after.family4RootSoid
                || prepared.family.objects.back().encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects.back().payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    queuez_report::push(stage, queuez::kBannerFamilyType, objectCount, written - beforeBytes, 1);
    return true;
}

[[nodiscard]] bool
append_roster_appearance_frame(Scratch& scratch,
                               const queuez::RosterAppearanceRefresh& refresh,
                               snapshot::Prepared& prepared,
                               const char* stage,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::array<std::byte, state::kBapNonceSize>& nonce,
                               std::span<std::byte> response,
                               std::size_t& written) noexcept {
    const std::size_t expectedObjects = refresh.includeRoster ? 2U : 1U;
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t beforeBytes = written;
    if (objectCount != expectedObjects || prepared.family.type != queuez::kRosterFamilyType
        || prepared.family.rootSoid != refresh.after.family3RootSoid
        || prepared.family.version != refresh.after.family3Version || prepared.family.flags != 0
        || prepared.family.objects.front().id != middleware::datagen::kRosterCharacterObjectId
        || prepared.family.objects.front().version != refresh.characterSoid
        || prepared.family.objects.front().encoding != middleware::queuez::Encoding::oodle
        || prepared.family.objects.front().payload.empty()
        || (refresh.includeRoster
            && (prepared.family.objects.back().id != middleware::datagen::kRosterObjectId
                || prepared.family.objects.back().version != refresh.after.family3RootSoid
                || prepared.family.objects.back().encoding != middleware::queuez::Encoding::oodle
                || prepared.family.objects.back().payload.empty()))
        || !queuez_frame::append(scratch,
                                 prepared.family,
                                 prepared.rawClearSize,
                                 prepared.compressedClearSize,
                                 key,
                                 nonce,
                                 response,
                                 written)) {
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    queuez_report::push(stage, queuez::kRosterFamilyType, objectCount, written - beforeBytes, 1);
    return true;
}

} // namespace

// sent twice per boot at the same version: family zero hits the state-1 race with no svc-12 re-push, so a rejected first pair would blank the banner for the run
bool append_banner_notification(Scratch& scratch,
                                const queuez::SessionState& before,
                                std::uint64_t familyRootSoid,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written,
                                queuez::SessionState& after) noexcept {
    after = before;
    ensure_account_canonical();
    // the pair names the first character when none is picked; the client's family-zero record accepts a snapshot for about ten seconds, so holding it for the pick times out
    if (state::account::banner_character_soid(state::account_snapshot()) == 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=queuez stage=banner result=skip reason=nocharacter");
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_banner(
            scratch, familyRootSoid, queuez::kInitialFamilyVersion, 0, prepared)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=banner result=fail reason=prepare");
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t beforeBytes = written;
    if (!queuez_frame::append(scratch,
                              prepared.family,
                              prepared.rawClearSize,
                              prepared.compressedClearSize,
                              key,
                              nonce,
                              response,
                              written)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=banner result=fail reason=frame");
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    // the client now holds this pair so the ladder owns it; otherwise an unsubscribe leaves nothing for the next pick to release
    const std::uint64_t delivered =
        state::account::banner_character_soid(state::account_snapshot());
    if (!after.family0Active && delivered != 0) {
        after.family0Active = true;
        after.family0Character = delivered;
        after.family0Version = queuez::kInitialFamilyVersion;
    }
    queuez_report::push("banner",
                        prepared.family.type,
                        objectCount,
                        written - beforeBytes,
                        queuez_report::kNoRecordOutcome);
    return true;
}

bool append_banner_move_notification(Scratch& scratch,
                                     const queuez::SessionState& before,
                                     std::uint64_t selectedCharacter,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::array<std::byte, state::kBapNonceSize>& nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written,
                                     queuez::SessionState& after) noexcept {
    bool publish = false;
    bool incremental = false;
    after = before;
    ensure_account_canonical();
    const char* reason = nullptr;
    if (!queuez::stage_family0_subscription(
            before, selectedCharacter, publish, incremental, after)) {
        reason = "stage";
    } else if (before.family4RootSoid == 0) {
        reason = "no_root";
    }
    if (reason != nullptr) {
        report_skip(reason);
        after = before;
        return false;
    }
    // release nothing: deleting the key the same frame re-adds tears the family down
    const bool republish = !publish;
    if (republish) {
        incremental = false;
        after.family0Version = before.family0Version + 1;
    }
    const char* const stage = republish ? "banner_republish" : "banner_move";
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_banner(scratch,
                                  before.family4RootSoid,
                                  after.family0Version,
                                  incremental ? before.family0Character : 0,
                                  prepared)) {
        report_fail(stage, "prepare");
        after = before;
        return false;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t beforeBytes = written;
    if (!queuez_frame::append(scratch,
                              prepared.family,
                              prepared.rawClearSize,
                              prepared.compressedClearSize,
                              key,
                              nonce,
                              response,
                              written)) {
        report_fail(stage, "frame");
        after = before;
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    queuez_report::push(stage,
                        prepared.family.type,
                        objectCount,
                        written - beforeBytes,
                        queuez_report::kNoRecordOutcome);
    return true;
}

bool append_equipment_appearance_refresh_notification(
    Scratch& scratch,
    const queuez::CharacterAppearanceRefresh& refresh,
    const state::PendingEquipmentSwap& mutation,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    if (!mutation.prepared || mutation.characterSoid != refresh.characterSoid) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_character_appearance_refresh(scratch,
                                                        refresh,
                                                        mutation.afterCharacter,
                                                        mutation.characterIndex,
                                                        mutation.nativeEquipmentSlot,
                                                        false,
                                                        prepared)) {
        return false;
    }
    return append_appearance_frame(
        scratch, refresh, prepared, "equip_appearance", key, nonce, response, written);
}

bool append_socket_appearance_refresh_notification(
    Scratch& scratch,
    const queuez::CharacterAppearanceRefresh& refresh,
    const state::PendingSocketPlug& mutation,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    if (!mutation.prepared || !mutation.targetEquipped
        || mutation.characterSoid != refresh.characterSoid
        || mutation.itemIndex >= mutation.afterCharacter.equipment.slots.size()
        || !mutation.afterCharacter.equipment.slots[mutation.itemIndex].has_value()) {
        return false;
    }
    const state::account::inventory::Item& target =
        *mutation.afterCharacter.equipment.slots[mutation.itemIndex];
    if (target.instanceSoid != mutation.targetInstanceSoid) {
        return false;
    }
    state::build_data::items::details::Definition detail{};
    std::uint8_t nativeEquipmentSlot = 0;
    if (!state::build_data::find_configured_item_detail(mutation.targetDefinitionIndex, detail)
        || detail.definitionIndex != mutation.targetDefinitionIndex
        || detail.definitionHash != mutation.targetDefinitionHash
        || detail.bucketId != mutation.targetBucketId
        || !state::account::inventory::resolve_native_equipment_slot(
            mutation.targetDefinitionHash, detail.equipmentSlot, nativeEquipmentSlot)
        || static_cast<std::size_t>(nativeEquipmentSlot)
               >= state::build_data::items::details::kEquipmentSlotCount) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_character_appearance_refresh(
            scratch,
            refresh,
            mutation.afterCharacter,
            mutation.characterIndex,
            nativeEquipmentSlot,
            false,
            prepared)) {
        return false;
    }
    return append_appearance_frame(
        scratch, refresh, prepared, "socket_appearance", key, nonce, response, written);
}

bool append_equipment_roster_refresh_notification(
    Scratch& scratch,
    const queuez::RosterAppearanceRefresh& refresh,
    const state::PendingEquipmentSwap& mutation,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    if (!mutation.prepared || !refresh.includeRoster
        || mutation.characterSoid != refresh.characterSoid) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_roster_appearance_refresh(
            scratch, refresh, mutation.afterCharacter, mutation.characterIndex, prepared)) {
        return false;
    }
    return append_roster_appearance_frame(
        scratch, refresh, prepared, "equip_roster", key, nonce, response, written);
}

bool append_socket_roster_refresh_notification(Scratch& scratch,
                                               const queuez::RosterAppearanceRefresh& refresh,
                                               const state::PendingSocketPlug& mutation,
                                               std::span<const std::byte, state::kAesKeySize> key,
                                               std::array<std::byte, state::kBapNonceSize>& nonce,
                                               std::span<std::byte> response,
                                               std::size_t& written) noexcept {
    if (!mutation.prepared || !mutation.targetEquipped || refresh.includeRoster
        || mutation.characterSoid != refresh.characterSoid
        || mutation.itemIndex >= mutation.afterCharacter.equipment.slots.size()
        || !mutation.afterCharacter.equipment.slots[mutation.itemIndex].has_value()
        || mutation.afterCharacter.equipment.slots[mutation.itemIndex]->instanceSoid
               != mutation.targetInstanceSoid) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_roster_appearance_refresh(
            scratch, refresh, mutation.afterCharacter, mutation.characterIndex, prepared)) {
        return false;
    }
    return append_roster_appearance_frame(
        scratch, refresh, prepared, "socket_roster", key, nonce, response, written);
}

bool append_ability_appearance_refresh_notification(
    Scratch& scratch,
    const queuez::CharacterAppearanceRefresh& refresh,
    const state::PendingAbilitySelection& mutation,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written) noexcept {
    if (!mutation.prepared || mutation.characterSoid != refresh.characterSoid) {
        return false;
    }
    constexpr std::uint8_t kSubclassNativeSlot = 0;
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_character_appearance_refresh(scratch,
                                                        refresh,
                                                        mutation.afterCharacter,
                                                        mutation.characterIndex,
                                                        kSubclassNativeSlot,
                                                        false,
                                                        prepared)) {
        return false;
    }
    return append_appearance_frame(
        scratch, refresh, prepared, "ability_appearance", key, nonce, response, written);
}

bool append_ability_roster_refresh_notification(Scratch& scratch,
                                                const queuez::RosterAppearanceRefresh& refresh,
                                                const state::PendingAbilitySelection& mutation,
                                                std::span<const std::byte, state::kAesKeySize> key,
                                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                                std::span<std::byte> response,
                                                std::size_t& written) noexcept {
    if (!mutation.prepared || refresh.includeRoster
        || mutation.characterSoid != refresh.characterSoid) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_roster_appearance_refresh(
            scratch, refresh, mutation.afterCharacter, mutation.characterIndex, prepared)) {
        return false;
    }
    return append_roster_appearance_frame(
        scratch, refresh, prepared, "ability_roster", key, nonce, response, written);
}

bool append_remote_appearance_notification(Scratch& scratch,
    remote_appearance::Published before,
    std::span<const std::byte,state::kAesKeySize> key,
    std::array<std::byte,state::kBapNonceSize>& nonce,
    std::span<std::byte> response,std::size_t& written,
    remote_appearance::Published& after) noexcept {
    after=before;
    const auto account=state::account_snapshot();
    const auto selected=state::account::selected_character_soid(account);
    if (!before.valid || !before.root || !before.character || before.root!=account.primarySoid
        || before.version<0 || before.version==(std::numeric_limits<std::int32_t>::max)()
        || !selected) return false;
    const auto version=before.version+1;
    snapshot::Prepared prepared{};
    if (selected!=before.character) {
        if (!snapshot::prepare_banner(scratch,before.root,version,before.character,prepared)
            || !queuez_frame::append(scratch,prepared.family,prepared.rawClearSize,
                prepared.compressedClearSize,key,nonce,response,written)) return false;
        middleware::secure_channel::advance_nonce(nonce);
    } else {
        std::size_t index=0;
        while(index<account.characterCount && account.characters[index].soid!=selected) ++index;
        if(index==account.characterCount)return false;
        queuez::CharacterAppearanceRefresh refresh{};
        refresh.characterSoid=selected;
        refresh.after.family4RootSoid=before.root;
        refresh.after.family0Character=selected;
        refresh.after.family0Version=version;
        refresh.after.family0Active=true;
        // E018F5 dispatches E05DA0 for changed resident records and family 0 slot 1 then calls FAB000 to dirty the remote resource list
        // a tombstone + insert takes E05EB0 instead, with no invalidation, and the profile waits for unrequested assets
        if (!snapshot::prepare_character_appearance_refresh(scratch,refresh,
                account.characters[index],index,0,false,prepared)
            || !append_appearance_frame(scratch,refresh,prepared,"remote_appearance_build",
                key,nonce,response,written)) return false;
    }
    after={before.root,selected,version,true};
    return true;
}

bool append_account_resync_appearance_notification(
    Scratch& scratch,
    const queuez::SessionState& before,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written,
    queuez::SessionState& after) noexcept {
    after = before;
    ensure_account_canonical();
    if (!before.family0Active) {
        return true;
    }
    const state::AccountState account = state::account_snapshot();
    const std::uint64_t selected = state::account::selected_character_soid(account);
    // nothing is picked after a delete or before any pick; failing here aborted the whole resync and stranded the version the reply promised
    if (selected == 0) {
        return true;
    }
    if (before.family0Character != selected) {
        return append_banner_move_notification(
            scratch, before, selected, key, nonce, response, written, after);
    }
    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].soid == selected) {
            characterIndex = index;
            break;
        }
    }
    queuez::CharacterAppearanceRefresh refresh{};
    snapshot::Prepared prepared{};
    if (characterIndex >= account.characterCount
        || !queuez::stage_character_appearance_refresh(before, selected, refresh)
        || !snapshot::prepare_character_appearance_refresh(
            scratch, refresh, account.characters[characterIndex], characterIndex, 0, true, prepared)
        || !append_appearance_frame(
            scratch, refresh, prepared, "peer_resync_appearance", key, nonce, response, written)) {
        return false;
    }
    after = refresh.after;
    return true;
}

bool append_account_resync_roster_notification(Scratch& scratch,
                                               const queuez::SessionState& before,
                                               std::span<const std::byte, state::kAesKeySize> key,
                                               std::array<std::byte, state::kBapNonceSize>& nonce,
                                               std::span<std::byte> response,
                                               std::size_t& written,
                                               queuez::SessionState& after) noexcept {
    after = before;
    ensure_account_canonical();
    if (!before.family3Active) {
        return true;
    }
    const state::AccountState account = state::account_snapshot();
    const std::uint64_t selected = state::account::selected_character_soid(account);
    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].soid == selected) {
            characterIndex = index;
            break;
        }
    }
    // step 27 still reads the roster's character count; skipping left the deleted character counted and the create slot never returned
    if (selected == 0) {
        if (before.family3RootSoid == 0 || before.family3RootSoid != account.primarySoid
            || before.family3Version == (std::numeric_limits<std::int32_t>::max)()) {
            return true;
        }
        queuez::SessionState staged = before;
        ++staged.family3Version;
        snapshot::Prepared prepared{};
        if (!queuez::valid(staged)
            || !snapshot::prepare_roster_list_refresh(
                scratch, staged.family3Version, before.family3RootSoid, prepared)
            || prepared.family.objects.size() != 1U) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync_roster_list result=skip reason=build");
            return true;
        }
        const std::size_t beforeBytes = written;
        if (!queuez_frame::append(scratch,
                                  prepared.family,
                                  prepared.rawClearSize,
                                  prepared.compressedClearSize,
                                  key,
                                  nonce,
                                  response,
                                  written)) {
            return false;
        }
        middleware::secure_channel::advance_nonce(nonce);
        queuez_report::push(
            "peer_resync_roster_list", queuez::kRosterFamilyType, 1, written - beforeBytes, 1);
        after = staged;
        return true;
    }
    queuez::RosterAppearanceRefresh refresh{};
    snapshot::Prepared prepared{};
    if (characterIndex >= account.characterCount
        || !queuez::stage_roster_appearance_refresh(before, selected, true, refresh)
        || !snapshot::prepare_roster_appearance_refresh(
            scratch, refresh, account.characters[characterIndex], characterIndex, prepared)
        || !append_roster_appearance_frame(
            scratch, refresh, prepared, "peer_resync_roster", key, nonce, response, written)) {
        return false;
    }
    after = refresh.after;
    return true;
}

} // namespace dawn::server::bap::encrypted::push
