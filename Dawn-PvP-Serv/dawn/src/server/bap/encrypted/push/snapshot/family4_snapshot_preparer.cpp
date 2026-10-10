#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family4/account/account_encoder.h"
#include "../../../../../middleware/datagen/family4/account/layout.h"
#include "../../../../../middleware/datagen/family4/character/character_encoder.h"
#include "../../../../../middleware/datagen/family4/character/layout.h"
#include "../../../../../middleware/datagen/family4/instance/layout.h"
#include "../../../../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

namespace family4_datagen = middleware::datagen::family4;

[[nodiscard]] bool publish(const middleware::queuez::Subscription& subscription,
                           std::size_t objectCount,
                           std::size_t compressedExtent,
                           const Reservation& reservation,
                           Prepared& staged,
                           Prepared& output) noexcept {
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kAccountFamilyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(objectCount),
    };
    return commit(staged, output);
}

} // namespace

bool prepare(Scratch& scratch,
             const middleware::queuez::Subscription& subscription,
             std::uint32_t accountObjectId,
             const Reservation& reservation,
             Prepared& prepared,
             std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions) noexcept {
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("reservation");
    }
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (family4_datagen::account::layout::kObjectSize > rawStorage.size()) {
        return report_failure("account_storage");
    }
    if (!state::ensure_profile_item_identities()) {
        return report_failure("profile_identities");
    }
    const state::AccountState account = state::account_snapshot();
    if (!state::account::valid(account)) {
        return report_failure("account_state");
    }

    const std::optional<std::size_t> selectedIndex = find_character_index(account);
    Resolved selected{};
    if (selectedIndex.has_value() && !resolve(account, *selectedIndex, selected)) {
        return report_failure("selection");
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize,
                   reservation.rawWriteOffset + family4_datagen::account::layout::kObjectSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    const auto accountBytes = rawStorage.first(family4_datagen::account::layout::kObjectSize);
    if (!family4_datagen::account::encode(account, accountBytes)) {
        return report_failure("account_object");
    }
    if (!append_object(scratch,
                       accountBytes,
                       accountObjectId,
                       account.primarySoid,
                       staged.objects[kAccountObjectIndex],
                       compressedExtent)) {
        return report_failure("account_object");
    }
    const std::size_t itemBaseIndex =
        selectedIndex.has_value() ? kFirstItemObjectIndex : kFirstItemObjectIndexUnselected;
    if (selectedIndex.has_value()) {
        staged.rawClearSize = (std::max)(staged.rawClearSize,
                                         reservation.rawWriteOffset
                                             + family4_datagen::character::layout::kObjectSize);
        if (family4_datagen::character::layout::kObjectSize > rawStorage.size()) {
            return report_failure("character_storage");
        }
        const auto characterBytes =
            rawStorage.first(family4_datagen::character::layout::kObjectSize);
        const state::CharacterState& selectedCharacter =
            account.characters[selected.characterIndex];
        if (!family4_datagen::character::encode(
                selectedCharacter, selected.loadout, selected.lightEvaluation, characterBytes)) {
            return report_failure("character_encode");
        }
        family4_datagen::character::apply_acquisition_feedback(
            *reinterpret_cast<family4_datagen::character::layout::Object*>(characterBytes.data()), acquisitions);
        if (!append_object(scratch,
                           characterBytes,
                           selected.characterObjectId,
                           selectedCharacter.soid,
                           staged.objects[kCharacterObjectIndex],
                           compressedExtent)) {
            return report_failure("character_object");
        }
    }
    // every roster character needs item records; the equip-summary reader looks up an instance with no null check
    std::uint32_t itemInstanceObjectId = 0;
    if (!middleware::datagen::object_id(
            kAccountFamilyType, kItemDefinitionSlotIndex, itemInstanceObjectId)) {
        return report_failure("item_object_id");
    }
    std::size_t itemCursor = 0;
    for (std::size_t characterIndex = 0; characterIndex < account.characterCount;
         ++characterIndex) {
        family4_datagen::loadout::ResolvedInstances instances{};
        if (!family4_datagen::loadout::resolve_owned_instances(
                account, characterIndex, instances)) {
            return report_failure("loadout");
        }
        if (instances.itemCount != 0
            && !append_items(scratch,
                             rawStorage,
                             itemInstanceObjectId,
                             instances,
                             itemBaseIndex,
                             staged,
                             itemCursor,
                             compressedExtent)) {
            return report_failure("items");
        }
    }
    if (!append_profile_items(scratch,
                              rawStorage,
                              itemInstanceObjectId,
                              account,
                              itemBaseIndex,
                              staged,
                              itemCursor,
                              compressedExtent)) {
        return report_failure("profile_items");
    }
    if (!append_vault_items(scratch,
                            rawStorage,
                            itemInstanceObjectId,
                            account,
                            itemBaseIndex,
                            staged,
                            itemCursor,
                            compressedExtent)) {
        return report_failure("vault_items");
    }

    const std::size_t objectCount = itemBaseIndex + itemCursor;
    if (selectedIndex.has_value() && !acquisitions.empty()) {
        // newly resident instances must exist before the acquisition observer reads the character
        std::rotate(staged.objects.begin()+kCharacterObjectIndex,
                    staged.objects.begin()+kFirstItemObjectIndex,
                    staged.objects.begin()+objectCount);
    }
    if (itemCursor != 0) {
        staged.rawClearSize =
            (std::max)(staged.rawClearSize,
                       reservation.rawWriteOffset + family4_datagen::instance::layout::kObjectSize);
    }
    return publish(subscription, objectCount, compressedExtent, reservation, staged, prepared);
}

} // namespace dawn::server::bap::encrypted::push::snapshot
