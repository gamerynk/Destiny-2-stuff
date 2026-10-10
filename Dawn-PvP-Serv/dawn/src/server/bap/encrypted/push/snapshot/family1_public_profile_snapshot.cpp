#include <algorithm>
#include <array>
#include <cstdio>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/character_record/character_record_encoder.h"
#include "../../../../../middleware/datagen/character_record/layout.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/family1/family1_encoder.h"
#include "../../../../../middleware/datagen/family4/instance/layout.h"
#include "../../../../../middleware/datagen/family4/loadout/loadout_resolver.h"
#include "../../../../../middleware/datagen/family4/progression/progression_bank_keys.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

namespace character_record = middleware::datagen::character_record;
namespace family1 = middleware::datagen::family1;
namespace family4 = middleware::datagen::family4;

} // namespace

bool prepare_public_profile(Scratch& scratch,
                            const middleware::queuez::Subscription& subscription,
                            std::uint32_t rootObjectId,
                            const Reservation& reservation,
                            Prepared& prepared) noexcept {
    if (subscription.familyType != kPublicProfileFamilyType
        || subscription.familyRootSoid == 0 || rootObjectId == 0
        || reservation.rawWriteOffset > scratch.plaintext.size()
        || reservation.compressedWriteOffset > scratch.sealed.size()) {
        return report_failure("public_profile_reservation");
    }

    std::uint32_t characterObjectId = 0;
    std::uint32_t itemObjectId = 0;
    if (!middleware::datagen::object_id(kPublicProfileFamilyType,
                                        middleware::datagen::kPublicCharacterSlot,
                                        characterObjectId)
        || !middleware::datagen::object_id(kPublicProfileFamilyType,
                                           middleware::datagen::kPublicItemInstanceSlot,
                                           itemObjectId)) {
        return report_failure("public_profile_ids");
    }

    const state::AccountState account = state::account_snapshot();
    const auto selectedIndex = find_character_index(account);
    if (!state::account::valid(account) || account.primarySoid != subscription.familyRootSoid
        || !selectedIndex.has_value()) {
        return report_failure("public_profile_state");
    }

    Resolved selected{};
    family4::loadout::ResolvedInstances instances{};
    if (!resolve(account, *selectedIndex, selected)
        || !family4::loadout::resolve_instances(account, *selectedIndex, instances)
        || instances.itemCount > family1::kEquipmentCapacity) {
        return report_failure("public_profile_loadout");
    }
    const state::CharacterState& character = account.characters[*selectedIndex];

    const auto rawStorage = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    if (rawStorage.size() < family1::kBodySize) {
        return report_failure("public_profile_storage");
    }

    std::array<family4::progression::layout::Entry, family1::kProgressionCapacity>
        accountProgressions{};
    std::array<family4::progression::layout::Entry, family1::kProgressionCapacity>
        characterProgressions{};
    if (!family4::progression::key_bank(state::build_data::progressions::Scope::account,
                                        accountProgressions)
        || !family4::progression::key_bank(state::build_data::progressions::Scope::character,
                                           characterProgressions)) {
        return report_failure("public_profile_progressions");
    }

    Prepared staged{};
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    const auto root = rawStorage.first(family1::kRootSize);
    // PC 1.59 allocates one resident in Family-1 slot one; the 3D path follows root +8, so point it at the same account-keyed singleton
    if (!family1::encode_root(
            account.primarySoid, account.primarySoid, accountProgressions, root)
        || !append_object(scratch,
                          root,
                          rootObjectId,
                          account.primarySoid,
                          staged.objects[0],
                          compressedExtent)) {
        return report_failure("public_profile_root");
    }

    std::array<std::byte, character_record::kFamily0RecordSize> presentation{};
    if (!character_record::encode_family0(
            character, instances, selected.lightEvaluation.average, presentation)) {
        return report_failure("public_profile_appearance");
    }
    const auto identity =
        std::span<const std::byte>(presentation).first(character_record::layout::kIdentitySize);
    const auto appearance = std::span<const std::byte>(presentation).subspan(
        character_record::layout::kIdentitySize,
        character_record::layout::kAppearanceSize);
    const auto summary = std::span<const std::byte>(presentation).subspan(
        character_record::layout::kIdentitySize + character_record::layout::kAppearanceSize,
        character_record::layout::kSummarySize);
    const auto body = rawStorage.first(family1::kBodySize);
    if (!family1::encode_body(
            account.primarySoid,
            identity,
            characterProgressions,
            selected.loadout,
            appearance,
            summary,
            body)
        || !append_object(scratch,
                          body,
                          characterObjectId,
                          account.primarySoid,
                          staged.objects[1],
                          compressedExtent)) {
        return report_failure("public_profile_character");
    }

    std::size_t itemCursor = 0;
    if (instances.itemCount != 0
        && !append_items(scratch,
                         rawStorage,
                         itemObjectId,
                         instances,
                         2,
                         staged,
                         itemCursor,
                         compressedExtent)) {
        return report_failure("public_profile_items");
    }

    staged.rawClearSize = (std::max)(
        reservation.rawClearSize,
        reservation.rawWriteOffset
            + (std::max)(family1::kBodySize, family4::instance::layout::kObjectSize));
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(2 + itemCursor),
    };
    if (!commit(staged, prepared)) {
        return report_failure("public_profile_commit");
    }

    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(
        line.data(),
        line.size(),
        "ev=queuez stage=public_profile result=ok root=0x%016llX character=0x%016llX"
        " resident=0x%016llX model_link=0x%016llX"
        " items=%zu light=%d objects=%zu root_id=0x%08X character_id=0x%08X item_id=0x%08X",
        static_cast<unsigned long long>(account.primarySoid),
        static_cast<unsigned long long>(character.soid),
        static_cast<unsigned long long>(account.primarySoid),
        static_cast<unsigned long long>(account.primarySoid),
        itemCursor,
        selected.lightEvaluation.average,
        prepared.family.objects.size(),
        rootObjectId,
        characterObjectId,
        itemObjectId);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
