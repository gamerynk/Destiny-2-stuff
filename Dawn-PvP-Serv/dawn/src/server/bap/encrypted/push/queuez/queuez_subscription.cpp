#include <array>
#include <limits>

#include "../../../../../client/targets/game/content.h"
#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../../state/social/remote_profile_cache.h"
#include "../../../../../state/social/social_state.h"
#include "../../queuez/queuez_state_validation.h"
#include "../../queuez/staging/queuez_subscription_progress.h"
#include "../snapshot/snapshot.h"
#include "queuez_push_reporting.h"
#include "queuez_update_frame.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/account_store.h"
#endif

namespace dawn::server::bap::encrypted::push {
namespace {

[[nodiscard]] bool append_family4_companion(Scratch& scratch,
                                            const queuez::SessionState& before,
                                            std::uint64_t familyRootSoid,
                                            std::span<const std::byte, state::kAesKeySize> key,
                                            std::array<std::byte, state::kBapNonceSize>& nonce,
                                            std::span<std::byte> response,
                                            std::size_t& written,
                                            queuez::SessionState& after) noexcept {
    middleware::queuez::Subscription companion{};
    companion.familyType = queuez::kAccountFamilyType;
    companion.familyRootSoid = familyRootSoid;

    snapshot::Prepared prepared{};
    if (!snapshot::prepare_initial(scratch, companion, prepared)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=companion result=fail reason=prepare");
        return false;
    }
    // the record is still state 1 DECLARED and only state 2 accepts a snapshot; the first copy is expected to be rejected
    // so a refused staging still sends the frame and still owes the re-push
    queuez::SessionState staged = before;
    const bool resident = !prepared.family.objects.empty();
    const bool recorded =
        resident && queuez::stage_family4_snapshot(before, prepared.family, staged);
    if (!recorded) {
        staged = before;
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
                         "ev=queuez stage=companion result=fail reason=frame");
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    after = staged;
    queuez_report::push("companion",
                        queuez::kAccountFamilyType,
                        objectCount,
                        written - beforeBytes,
                        recorded ? 1 : 0);
    return true;
}

} // namespace

bool append_account_resync_notification(Scratch& scratch,
                                        const queuez::SessionState& before,
                                        std::span<const std::byte, state::kAesKeySize> key,
                                        std::array<std::byte, state::kBapNonceSize>& nonce,
                                        std::span<std::byte> response,
                                        std::size_t& written,
                                        queuez::SessionState& after,
                                        std::span<const middleware::datagen::family4::character::AcquisitionNotice> acquisitions) noexcept {
    after = before;
    ensure_account_canonical();
    if (!queuez::valid(before) || !before.family4Active || before.family4RootSoid == 0
        || before.family4Version == (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    snapshot::Prepared prepared{};
    if (!snapshot::prepare_family4_refresh(
            scratch, before.family4RootSoid, before.family4Version + 1, prepared, acquisitions)
        || !queuez::stage_family4_refresh(before, prepared.family, after)) {
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
        after = before;
        return false;
    }
    middleware::secure_channel::advance_nonce(nonce);
    queuez_report::push(
        "peer_resync", queuez::kAccountFamilyType, objectCount, written - beforeBytes, 1);
    return true;
}

void append_queuez_notification_for_account(
    Scratch& scratch,
    const queuez::SessionState& before,
    const middleware::queuez::Subscription& subscription,
    std::span<const std::byte, state::kAesKeySize> key,
    std::array<std::byte, state::kBapNonceSize>& nonce,
    std::span<std::byte> response,
    std::size_t& written,
    queuez::SessionState& after,
    bool& armsRepush,
    bool& armsBannerRepush,
    const state::social::RemoteAccount* liveRemote = nullptr,
    std::uint64_t authenticatedSelfRoot = 0,
    InlineSubscriptionUpdate* inlineUpdate = nullptr) noexcept {
    after = before;
    armsRepush = false;
    armsBannerRepush = false;
    // run before the family dispatch: family zero reads the account directly and family three builds before four, so a later migration splits the images
    ensure_account_canonical();
    if (subscription.familyType == queuez::kAccountFamilyType && before.family4Active
        && before.family4Version != queuez::kInitialFamilyVersion) {
        queuez_report::subscription_state("session");
    }

    bool publish = true;
    bool incremental = false;
    queuez::SessionState stagedAfter = before;
    if (subscription.familyType == queuez::kRosterFamilyType
        && !queuez::stage_family3_subscription(before, subscription, publish, stagedAfter)) {
        queuez_report::subscription_state("stage_family3");
        stagedAfter = before;
        // a failed mirror check must never send a version-zero roster into an active incremental ladder
        publish = false;
    }

    snapshot::Prepared prepared{};
    if (subscription.familyType == queuez::kBannerFamilyType) {
        const state::AccountState account = state::account_snapshot();
        // the first character stands in before any pick; the record accepts a snapshot only in the short window the subscribe opens
        const std::uint64_t selected = state::account::banner_character_soid(account);
        if (selected == 0) {
            stagedAfter.pendingBannerRoot = subscription.familyRootSoid;
            queuez_report::subscription_state("nocharacter");
            after = stagedAfter;
            return;
        }
        stagedAfter.pendingBannerRoot = 0;
        if (!queuez::stage_family0_subscription(
                before, selected, publish, incremental, stagedAfter)) {
            queuez_report::subscription_state("stage_family0");
            stagedAfter = before;
            stagedAfter.pendingBannerRoot = 0;
            incremental = false;
        }
        publish = true;
        if (!snapshot::prepare_banner(scratch,
                                      subscription.familyRootSoid,
                                      stagedAfter.family0Version,
                                      incremental ? before.family0Character : 0,
                                      prepared)) {
            queuez_report::subscription_failure("prepare_banner");
            return;
        }
    } else if (!snapshot::prepare_initial(
                   scratch, subscription, prepared, liveRemote, authenticatedSelfRoot)) {
        queuez_report::subscription_failure("prepare");
        return;
    }
    if (subscription.familyType == queuez::kAccountFamilyType && !prepared.family.objects.empty()
        && !queuez::stage_family4_snapshot(before, prepared.family, stagedAfter)) {
        queuez_report::subscription_state("stage_family4");
        stagedAfter = before;
    }
    // an empty full snapshot prunes the family; wiping the roster closes the gate the family-zero source list comes from
    // every other family needs it, since it moves a bodiless record to synced
    if (prepared.family.objects.empty() && subscription.familyType == queuez::kRosterFamilyType) {
        queuez_frame::clear_object_storage(
            scratch, prepared.rawClearSize, prepared.compressedClearSize);
        queuez_report::subscription_state("empty");
        after = stagedAfter;
        return;
    }
    if (!publish) {
        queuez_frame::clear_object_storage(
            scratch, prepared.rawClearSize, prepared.compressedClearSize);
        if (inlineUpdate != nullptr && queuez::prepare_family3_subscription_progress(
                before, subscription, inlineUpdate->bytes, inlineUpdate->size, stagedAfter)) {
            queuez_report::push("subscription_progress_inline", queuez::kRosterFamilyType,
                0, inlineUpdate->size, queuez_report::kNoRecordOutcome);
        }
        after = stagedAfter;
        return;
    }
    if (subscription.familyType == queuez::kRosterFamilyType
        && (!stagedAfter.family3Active || stagedAfter.family3RootSoid != subscription.familyRootSoid
            || stagedAfter.family3Version != queuez::kInitialFamilyVersion
            || prepared.family.type != queuez::kRosterFamilyType
            || prepared.family.rootSoid != stagedAfter.family3RootSoid
            || prepared.family.version != stagedAfter.family3Version
            || prepared.family.flags != middleware::queuez::kFullSnapshotFlag)) {
        queuez_report::subscription_failure("family3_ladder");
        return;
    }
    const std::size_t objectCount = prepared.family.objects.size();
    const std::size_t beforeBytes = written;
    bool encoded = false;
    if (inlineUpdate != nullptr) {
        const std::array families{prepared.family};
        encoded = middleware::queuez::encode_update(families, inlineUpdate->bytes, inlineUpdate->size);
        queuez_frame::clear_object_storage(scratch, prepared.rawClearSize, prepared.compressedClearSize);
    } else {
        encoded = queuez_frame::append(scratch, prepared.family, prepared.rawClearSize,
            prepared.compressedClearSize, key, nonce, response, written);
    }
    if (!encoded) {
        queuez_report::subscription_failure("frame");
        return;
    }
    if (inlineUpdate == nullptr) middleware::secure_channel::advance_nonce(nonce);
    queuez_report::push(inlineUpdate ? "snapshot_inline" : "snapshot",
                        subscription.familyType,
                        objectCount,
                        inlineUpdate ? inlineUpdate->size : written - beforeBytes,
                        queuez_report::kNoRecordOutcome);
    after = stagedAfter;
    // the client sends its subscribe just before writing the record state, so this first copy is refused; the delayed copy is the one that lands
    armsBannerRepush = subscription.familyType == queuez::kBannerFamilyType;

    if (subscription.familyType == queuez::kRosterFamilyType && !stagedAfter.family4Active) {
        queuez::SessionState companionAfter{};
        if (append_family4_companion(scratch,
                                     stagedAfter,
                                     subscription.familyRootSoid,
                                     key,
                                     nonce,
                                     response,
                                     written,
                                     companionAfter)) {
            after = companionAfter;
            armsRepush = true;
        }
        const queuez::SessionState bannerBefore = after;
        queuez::SessionState bannerDelivered{};
        if (append_banner_notification(scratch,
                                       bannerBefore,
                                       subscription.familyRootSoid,
                                       key,
                                       nonce,
                                       response,
                                       written,
                                       bannerDelivered)) {
            after = bannerDelivered;
            armsBannerRepush = true;
        }
    }
}

void append_queuez_notification(Scratch& scratch,
                                const queuez::SessionState& before,
                                const middleware::queuez::Subscription& subscription,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written,
                                queuez::SessionState& after,
                                bool& armsRepush,
                                bool& armsBannerRepush,
                                remote_appearance::Published* foreignAppearance,
                                InlineSubscriptionUpdate* inlineUpdate) noexcept {
    if (foreignAppearance) *foreignAppearance = {};
    const state::AccountState local = state::account_snapshot();
    const bool remoteReadableFamily =
        subscription.familyType == queuez::kBannerFamilyType
        || subscription.familyType == middleware::datagen::kPublicProfileFamily
        || subscription.familyType == queuez::kSocialRosterFamilyType
        || subscription.familyType == queuez::kRosterFamilyType
        || subscription.familyType == queuez::kAccountFamilyType
        || subscription.familyType == middleware::datagen::kFireteamFamily
        || subscription.familyType == middleware::datagen::kJoinFamily;
    if (!remoteReadableFamily || subscription.familyRootSoid == local.primarySoid) {
        append_queuez_notification_for_account(scratch,
                                               before,
                                               subscription,
                                               key,
                                               nonce,
                                               response,
                                               written,
                                               after,
                                               armsRepush,
                                               armsBannerRepush,
                                               nullptr,
                                               local.primarySoid,
                                               inlineUpdate);
        return;
    }

    state::AccountState remote{};
    if (!state::social::remote_profile_cache::resolve(
            subscription.familyRootSoid, remote)) {
        after = before;
        armsRepush = false;
        armsBannerRepush = false;
        queuez_report::subscription_failure("remote_profile");
        return;
    }
    state::social::RemoteAccount liveRemote{};
    const state::social::RemoteAccount* liveRemoteView = nullptr;
    if (subscription.familyType == middleware::datagen::kFireteamFamily
        || subscription.familyType == middleware::datagen::kJoinFamily) {
#if defined(RESTORATION_DEDICATED_SERVER)
        if (restoration::account_store::live_friend(
                local.primarySoid, subscription.familyRootSoid, liveRemote)) {
            liveRemoteView = &liveRemote;
        }
#else
        const state::social::Snapshot social = state::social::snapshot();
        for (std::size_t index = 0; index < social.remoteCount; ++index) {
            if (social.remotes[index].accountSoid == subscription.familyRootSoid
                && social.remotes[index].relationship
                       == state::social::Relationship::friendAccount) {
                liveRemote = social.remotes[index];
                liveRemoteView = &liveRemote;
                break;
            }
        }
#endif
    }
    state::activity::ActivityState remoteActivity{};
    const state::AccountRuntimeContext context{&remote, &remoteActivity, {}, {}};
    const state::ScopedAccountRuntime scope(context);

    if (subscription.familyType == queuez::kBannerFamilyType) {
        // family 0 is keyed by {family, root} on the client; reusing the local ladder for a remote root would release the joiner's selected character
        // stage the remote root as its own FULL v0 record
        queuez::SessionState remoteBefore{};
        queuez::SessionState remoteAfter{};
        const auto priorWritten = written;
        append_queuez_notification_for_account(scratch,
                                               remoteBefore,
                                               subscription,
                                               key,
                                               nonce,
                                               response,
                                               written,
                                               remoteAfter,
                                               armsRepush,
                                               armsBannerRepush,
                                               liveRemoteView, 0, inlineUpdate);
        if (foreignAppearance && (written > priorWritten || (inlineUpdate && inlineUpdate->size != 0)) && remoteAfter.family0Active
            && remoteAfter.family0Character != 0 && remoteAfter.family0Version == 0) {
            *foreignAppearance = {subscription.familyRootSoid, remoteAfter.family0Character,
                                  remoteAfter.family0Version, true};
        }
        after = before;
        return;
    }

    append_queuez_notification_for_account(scratch,
                                           before,
                                           subscription,
                                           key,
                                           nonce,
                                           response,
                                           written,
                                           after,
                                           armsRepush,
                                           armsBannerRepush,
                                           liveRemoteView, 0, inlineUpdate);
}

} // namespace dawn::server::bap::encrypted::push
