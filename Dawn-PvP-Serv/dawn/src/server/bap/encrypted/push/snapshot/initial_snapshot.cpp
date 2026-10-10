#include <Windows.h>

#include <array>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"
#include "snapshot_storage.h"

namespace dawn::server::bap::encrypted::push::snapshot {
namespace {

constexpr std::size_t kEmptyReportCapacity = 64;

} // namespace

bool preflight_required_families() noexcept {
    static Scratch scratch{};
    // a fresh account has no persisted selection, so validate against an isolated preview, never the account the next real player signs into
    static state::AccountState account{};
    static state::activity::ActivityState previewActivity{};
    account = state::account_snapshot();
    previewActivity = {};
    static Prepared accountPrepared{};
    static Prepared socialRosterPrepared{};
    static Prepared rosterPrepared{};
    static Prepared bannerPrepared{};
    for (Prepared* prepared : {&accountPrepared, &socialRosterPrepared,
                               &rosterPrepared, &bannerPrepared}) {
        prepared->family = {};
        prepared->objects = {};
        prepared->rawClearSize = 0;
        prepared->compressedClearSize = 0;
    }
    SecureZeroMemory(&scratch, sizeof scratch);
    if (!state::account::valid(account) || account.primarySoid == 0 || account.characterCount == 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=queuez stage=preflight result=fail family=account reason=state");
        return false;
    }

    if (!find_character_index(account).has_value()) {
        account.characters[0].selected = true;
    }
    const state::AccountRuntimeContext previewRuntime{&account, &previewActivity};
    const state::ScopedAccountRuntime previewScope(previewRuntime);

    middleware::queuez::Subscription accountSubscription{};
    accountSubscription.familyType = kAccountFamilyType;
    accountSubscription.familyRootSoid = account.primarySoid;
    if (!prepare_initial(scratch, accountSubscription, accountPrepared)
        || accountPrepared.family.objects.empty()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=queuez stage=preflight result=fail family=4");
        return false;
    }

    SecureZeroMemory(&scratch, sizeof scratch);
    middleware::queuez::Subscription socialRosterSubscription{};
    socialRosterSubscription.familyType = kSocialRosterFamilyType;
    socialRosterSubscription.familyRootSoid = account.primarySoid;
    if (!prepare_initial(scratch, socialRosterSubscription, socialRosterPrepared)
        || socialRosterPrepared.family.objects.size() != 2U) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=queuez stage=preflight result=fail family=2");
        return false;
    }

    SecureZeroMemory(&scratch, sizeof scratch);
    middleware::queuez::Subscription rosterSubscription{};
    rosterSubscription.familyType = kRosterFamilyType;
    rosterSubscription.familyRootSoid = account.primarySoid;
    if (!prepare_initial(scratch, rosterSubscription, rosterPrepared)
        || rosterPrepared.family.objects.size() != account.characterCount + 1U) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=queuez stage=preflight result=fail family=3");
        return false;
    }

    SecureZeroMemory(&scratch, sizeof scratch);
    if (!prepare_banner(scratch,
                        account.primarySoid,
                        kInitialFamilyVersion,
                        0,
                        bannerPrepared)
        || bannerPrepared.family.objects.size() != 2U) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::error,
                         "ev=queuez stage=preflight result=fail family=0");
        return false;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::info,
                     "ev=queuez stage=preflight result=ok families=0,2,3,4");
    return true;
}

bool prepare_initial(Scratch& scratch,
                     const middleware::queuez::Subscription& subscription,
                     Prepared& prepared,
                     const state::social::RemoteAccount* liveRemote,
                     std::uint64_t authenticatedSelfRoot,
                     std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions) noexcept {
    const Reservation reservation = reserve_prior(scratch, prepared);
    Prepared staged{};
    staged.rawClearSize = reservation.rawClearSize;
    staged.compressedClearSize = reservation.compressedClearSize;
    const std::uint32_t slotIndex = subscription.familyType == kAccountFamilyType
                                        ? kAccountDefinitionSlotIndex
                                        : kRosterDefinitionSlotIndex;
    std::uint32_t objectId = 0;
    const bool hasDefinition =
        middleware::datagen::object_id(subscription.familyType, slotIndex, objectId);
    bool success = false;
    if (subscription.familyType == kPublicProfileFamilyType && hasDefinition) {
        success = prepare_public_profile(scratch, subscription, objectId, reservation, staged);
    } else if (subscription.familyType == kSocialRosterFamilyType && hasDefinition) {
        success = prepare_social_roster(scratch, subscription, objectId, reservation, staged);
    } else if (subscription.familyType == kRosterFamilyType && hasDefinition) {
        success = prepare_roster(scratch, subscription, objectId, reservation, staged);
    } else if (subscription.familyType == kAccountFamilyType && hasDefinition) {
        success = prepare(scratch, subscription, objectId, reservation, staged, acquisitions);
    } else if (subscription.familyType == kFireteamFamilyType && hasDefinition) {
        success = prepare_fireteam(scratch, subscription, reservation, staged, liveRemote,
                                   authenticatedSelfRoot);
    } else if (subscription.familyType == kJoinFamilyType && hasDefinition) {
        success = prepare_join(scratch, subscription, reservation, staged, liveRemote);
    }
    if (!success) {
        // every builder failure lands here; an empty family four never sets family4Active, which refuses every later character pick
        std::array<char, kEmptyReportCapacity> line{};
        const int count = std::snprintf(line.data(),
                                        line.size(),
                                        "ev=queuez stage=snapshot result=empty family=%u",
                                        static_cast<unsigned>(subscription.familyType));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        staged.objects = {};
        staged.rawClearSize = reservation.rawClearSize;
        staged.compressedClearSize = reservation.compressedClearSize;
        staged.family = middleware::queuez::Family{
            subscription.familyType,
            subscription.familyRootSoid,
            kInitialFamilyVersion,
            middleware::queuez::kFullSnapshotFlag,
            {},
        };
        success = true;
    }
    if (!success || !commit(staged, prepared)) {
        clear_after(scratch, reservation);
        return false;
    }
    return true;
}

bool prepare_family4_refresh(Scratch& scratch,
                             std::uint64_t familyRootSoid,
                             std::int32_t version,
                             Prepared& prepared,
                             std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions) noexcept {
    if (familyRootSoid == 0 || version <= kInitialFamilyVersion) {
        return false;
    }
    middleware::queuez::Subscription subscription{};
    subscription.familyType = kAccountFamilyType;
    subscription.familyRootSoid = familyRootSoid;
    if (!prepare_initial(scratch, subscription, prepared, nullptr, 0, acquisitions) || prepared.family.objects.empty()
        || prepared.family.type != kAccountFamilyType || prepared.family.rootSoid != familyRootSoid
        || prepared.family.flags != middleware::queuez::kFullSnapshotFlag) {
        return false;
    }
    prepared.family.version = version;
    return true;
}

} // namespace dawn::server::bap::encrypted::push::snapshot
