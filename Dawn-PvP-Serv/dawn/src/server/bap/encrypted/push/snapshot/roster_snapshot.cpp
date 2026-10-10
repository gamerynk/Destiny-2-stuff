#include <algorithm>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/character_record/character_record_encoder.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family3/family3_roster.h"
#include "../../../../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../../../../state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

namespace character_record = middleware::datagen::character_record;

[[nodiscard]] bool append_character_records(Scratch& scratch,
                                            const state::AccountState& account,
                                            std::size_t& rawExtent,
                                            std::size_t& compressedExtent,
                                            Prepared& staged,
                                            std::size_t& objectCount) noexcept {
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        middleware::datagen::family4::loadout::ResolvedInstances instances{};
        std::int32_t light = 0;
        if (!middleware::datagen::family4::loadout::resolve_instances(account, index, instances)
            || !state::equipment::light::resolution::character_light(account, index, light)) {
            return false;
        }
        if (rawExtent > scratch.plaintext.size()
            || scratch.plaintext.size() - rawExtent < character_record::kFamily3RecordSize
            || objectCount >= staged.objects.size()) {
            return false;
        }
        const auto record =
            std::span(scratch.plaintext).subspan(rawExtent, character_record::kFamily3RecordSize);
        if (!character_record::encode_family3(
                account.characters[index], instances, light, record)) {
            return false;
        }
        std::size_t compressedSize = 0;
        if (!compress_object(scratch,
                             record,
                             middleware::datagen::kRosterCharacterObjectId,
                             account.characters[index].soid,
                             compressedExtent,
                             staged.objects[objectCount],
                             compressedSize)) {
            return false;
        }
        compressedExtent += compressedSize;
        ++objectCount;
        rawExtent += character_record::kFamily3RecordSize;
    }
    return true;
}

} // namespace

bool prepare_roster(Scratch& scratch,
                    const middleware::queuez::Subscription& subscription,
                    std::uint32_t objectId,
                    const Reservation& reservation,
                    Prepared& prepared) noexcept {
    state::AccountState account = state::account_snapshot();
    if (reservation.rawWriteOffset > scratch.plaintext.size()) {
        return false;
    }
    // re-customize: step 27 reads the family-3 roster count and zero sends the client to character creation
    // while a mark is pending report no characters so the creator opens; the family-4 object keeps the real list
    if (state::customize_pending(account.primarySoid)) {
        account.characterCount = 0;
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=customize stage=roster result=creator_route count=0");
    }
    const auto destination = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    std::size_t rawSize = 0;
    if (!middleware::datagen::family3::encode_roster(account, destination, rawSize)) {
        return false;
    }
    Prepared staged{};
    std::size_t objectCount = 0;
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    // every queuez object goes out Oodle-encoded; the client has no raw-object path here
    std::size_t listCompressed = 0;
    if (!compress_object(scratch,
                         destination.first(rawSize),
                         objectId,
                         account.primarySoid,
                         compressedExtent,
                         staged.objects[objectCount],
                         listCompressed)) {
        return false;
    }
    compressedExtent += listCompressed;
    ++objectCount;
    std::size_t rawExtent = reservation.rawWriteOffset + rawSize;
    if (!append_character_records(
            scratch, account, rawExtent, compressedExtent, staged, objectCount)) {
        return false;
    }
    staged.rawClearSize = (std::max)(reservation.rawClearSize, rawExtent);
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(objectCount),
    };
    return commit(staged, prepared);
}

bool prepare_roster_appearance_refresh(Scratch& scratch,
                                       const queuez::RosterAppearanceRefresh& refresh,
                                       const state::CharacterState& afterCharacter,
                                       std::size_t characterIndex,
                                       Prepared& prepared) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return false;
    }

    state::AccountState account = state::account_snapshot();
    if (!refresh.after.family3Active || refresh.after.family3RootSoid == 0
        || refresh.after.family3Version <= kInitialFamilyVersion || refresh.characterSoid == 0
        || afterCharacter.soid != refresh.characterSoid || characterIndex >= account.characterCount
        || account.characters[characterIndex].soid != refresh.characterSoid
        || account.primarySoid != refresh.after.family3RootSoid) {
        return false;
    }
    account.characters[characterIndex] = afterCharacter;
    if (!state::account::valid(account)
        || state::account::selected_character_soid(account) != refresh.characterSoid) {
        return false;
    }

    middleware::datagen::family4::loadout::ResolvedInstances instances{};
    std::int32_t light = 0;
    if (!middleware::datagen::family4::loadout::resolve_instances(
            account, characterIndex, instances)
        || !state::equipment::light::resolution::character_light(account, characterIndex, light)) {
        return false;
    }

    const std::size_t rosterSize =
        refresh.includeRoster ? middleware::datagen::family3::kRosterSize : 0U;
    const std::size_t rawSize = character_record::kFamily3RecordSize + rosterSize;
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (rawSize > rawStorage.size()) {
        return false;
    }
    const auto characterBytes = rawStorage.first(character_record::kFamily3RecordSize);
    if (!character_record::encode_family3(afterCharacter, instances, light, characterBytes)) {
        return false;
    }
    std::span<std::byte> rosterBytes{};
    if (refresh.includeRoster) {
        rosterBytes = rawStorage.subspan(character_record::kFamily3RecordSize, rosterSize);
        std::size_t encodedRosterSize = 0;
        if (!middleware::datagen::family3::encode_roster(account, rosterBytes, encodedRosterSize)
            || encodedRosterSize != rosterSize) {
            return false;
        }
    }

    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + rawSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t objectCount = 0;
    if (!append_object(scratch,
                       characterBytes,
                       middleware::datagen::kRosterCharacterObjectId,
                       refresh.characterSoid,
                       staged.objects[objectCount++],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return false;
    }
    if (refresh.includeRoster
        && !append_object(scratch,
                          rosterBytes,
                          middleware::datagen::kRosterObjectId,
                          account.primarySoid,
                          staged.objects[objectCount++],
                          compressedExtent)) {
        clear_after(scratch, reservation);
        return false;
    }
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kRosterFamilyType,
        refresh.after.family3RootSoid,
        refresh.after.family3Version,
        0,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return false;
    }
    return true;
}

bool prepare_roster_list_refresh(Scratch& scratch,
                                 std::int32_t version,
                                 std::uint64_t rootSoid,
                                 Prepared& prepared) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    if (reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()
        || version <= kInitialFamilyVersion || rootSoid == 0) {
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    if (account.primarySoid != rootSoid) {
        return false;
    }
    const std::size_t rosterSize = middleware::datagen::family3::kRosterSize;
    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (rosterSize > rawStorage.size()) {
        return false;
    }
    const auto rosterBytes = rawStorage.first(rosterSize);
    std::size_t encodedRosterSize = 0;
    if (!middleware::datagen::family3::encode_roster(account, rosterBytes, encodedRosterSize)
        || encodedRosterSize != rosterSize) {
        return false;
    }
    Prepared staged{};
    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + rosterSize);
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t objectCount = 0;
    if (!append_object(scratch,
                       rosterBytes,
                       middleware::datagen::kRosterObjectId,
                       account.primarySoid,
                       staged.objects[objectCount++],
                       compressedExtent)) {
        clear_after(scratch, reservation);
        return false;
    }
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        kRosterFamilyType,
        rootSoid,
        version,
        0,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return false;
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
