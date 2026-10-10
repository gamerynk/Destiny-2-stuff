// slot 0 is keyed by account SOID and gives a member key at +8; slot 1 is keyed by that key
// both must be in the same full snapshot or a later one-object full snapshot prunes the other

#include <array>
#include <cstdio>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/datagen/social_roster/social_roster_encoder.h"
#include "../../../../../state/account/inventory/inventory_state.h"
#include "../../../../../state/build_data/items/item_catalog.h"
#include "../../../../../state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

constexpr std::size_t kReportCapacity = 192;

[[nodiscard]] bool selected_emblem_definition_index(const state::AccountState& account,
                                                     std::size_t characterIndex,
                                                     std::uint16_t& index,
                                                     std::uint32_t& definitionHash) noexcept {
    if (characterIndex >= account.characterCount) {
        return false;
    }
    const auto& slot =
        account.characters[characterIndex]
            .equipment
            .slots[static_cast<std::size_t>(state::account::inventory::EquipmentSlot::emblem)];
    if (!slot.has_value()) {
        return false;
    }
    state::build_data::items::Definition definition{};
    if (!state::build_data::items::find_hash(slot->definitionHash, definition)) {
        return false;
    }
    index = definition.definitionIndex;
    definitionHash = slot->definitionHash;
    return true;
}

} // namespace

bool prepare_social_roster(Scratch& scratch,
                           const middleware::queuez::Subscription& subscription,
                           std::uint32_t objectId,
                           const Reservation& reservation,
                           Prepared& prepared) noexcept {
    (void)objectId;
    const state::AccountState account = state::account_snapshot();
    // offline accounts load with no selection, so the card named none and the roster drew an empty
    // emblem; fall back to the first character like the banner does
    std::optional<std::size_t> selectedIndex = find_character_index(account);
    if (!selectedIndex.has_value() && account.characterCount != 0) {
        selectedIndex = 0;
    }
    if (account.primarySoid == 0 || !selectedIndex.has_value()
        || reservation.rawWriteOffset > scratch.plaintext.size()) {
        return report_failure("social_roster_state");
    }
    const std::uint64_t selectedCharacterSoid = account.characters[*selectedIndex].soid;
    const std::int16_t activityIndex = roster_activity_index_locked(account.primarySoid);
    if (selectedCharacterSoid == 0) {
        return report_failure("social_roster_character");
    }

    std::int32_t power = 0;
    if (!state::equipment::light::resolution::character_light(account, *selectedIndex, power)) {
        return report_failure("social_roster_power");
    }
    std::uint16_t emblem = middleware::datagen::social_roster::kEmptyDefinitionIndex;
    std::uint32_t emblemHash = 0;
    if (!selected_emblem_definition_index(account, *selectedIndex, emblem, emblemHash)) {
        emblem = middleware::datagen::social_roster::kEmptyDefinitionIndex;
    }

    const auto destination = std::span(scratch.plaintext).subspan(reservation.rawWriteOffset);
    constexpr std::size_t kTotal = middleware::datagen::kSocialRosterDirectorySize
                                   + middleware::datagen::kSocialRosterMemberSize;
    if (destination.size() < kTotal) {
        return report_failure("social_roster_storage");
    }

    Prepared staged{};
    std::size_t objectCount = 0;
    std::size_t compressedExtent = reservation.compressedWriteOffset;
    std::size_t rawUsed = 0;

    const auto emit = [&](std::size_t size, std::uint32_t id, bool directory) noexcept {
        if (objectCount >= staged.objects.size()) {
            return false;
        }
        const auto body = destination.subspan(rawUsed, size);
        const bool encoded = directory
                                 ? middleware::datagen::social_roster::encode_directory(
                                       account.primarySoid,
                                       selectedCharacterSoid,
                                       body)
                                 : middleware::datagen::social_roster::encode_member(
                                       {account.primarySoid, power, emblem, activityIndex},
                                       body);
        if (!encoded) {
            return false;
        }
        std::size_t compressedSize = 0;
        if (!compress_object(scratch,
                             body,
                             id,
                             account.primarySoid,
                             compressedExtent,
                             staged.objects[objectCount],
                             compressedSize)) {
            return false;
        }
        compressedExtent += compressedSize;
        rawUsed += size;
        ++objectCount;
        return true;
    };

    if (!emit(middleware::datagen::kSocialRosterDirectorySize,
              middleware::datagen::kSocialRosterDirectoryObjectId,
              true)) {
        return report_failure("social_roster_directory");
    }
    if (!emit(middleware::datagen::kSocialRosterMemberSize,
              middleware::datagen::kSocialRosterMemberObjectId,
              false)) {
        return report_failure("social_roster_member");
    }

    staged.rawClearSize =
        (std::max)(reservation.rawClearSize, reservation.rawWriteOffset + rawUsed);
    staged.compressedClearSize = (std::max)(reservation.compressedClearSize, compressedExtent);
    staged.family = middleware::queuez::Family{
        subscription.familyType,
        subscription.familyRootSoid,
        kInitialFamilyVersion,
        middleware::queuez::kFullSnapshotFlag,
        std::span(staged.objects).first(objectCount),
    };
    if (!commit(staged, prepared)) {
        return report_failure("social_roster_commit");
    }

    std::array<char, kReportCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=queuez stage=social_roster result=ok soid=0x%016llX"
                                      " objects=%zu bytes=%zu power=%d emblem=%u hash=0x%08X activity=%d",
                                      static_cast<unsigned long long>(account.primarySoid),
                                      objectCount,
                                      rawUsed,
                                      power,
                                      static_cast<unsigned>(emblem),
                                      static_cast<unsigned>(emblemHash),
                                      static_cast<int>(activityIndex));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
