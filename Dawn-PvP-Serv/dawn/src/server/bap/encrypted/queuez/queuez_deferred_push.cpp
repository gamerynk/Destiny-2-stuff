#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/datagen/definitions.h"
#include "../../../../middleware/compression/oodle/runtime.h"
#include "../push/snapshot/snapshot.h"
#include "../../../../state/account/account_state.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../../state/social/remote_profile_cache.h"
#include "../internal.h"
#include "../push/activity/activity_keepalive_push.h"
#include "../push/activity/activity_retirement_push.h"
#include "queuez_state_validation.h"
#include "../../../gameplay/peer/peer_transport.h"
#include "../../runtime.h"
#include "../../../../middleware/secure_channel/runtime.h"

namespace dawn::server::bap::encrypted {
namespace {

constexpr std::size_t kRepushReportLimit = 96;
constexpr std::uint64_t kJoinRepushRetryMs = 500;
void report_repush(const char* stage, std::size_t bytes) noexcept {
    std::array<char, kRepushReportLimit> line{};
    const int count = std::snprintf(
        line.data(), line.size(), "ev=queuez stage=%s result=ok bytes=%zu", stage, bytes);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

[[nodiscard]] bool consume_account_resync(Session& session,
                                          Scratch& scratch,
                                          std::span<std::byte> response,
                                          std::size_t& written,
                                          bool& touchesScratch) noexcept {
    if (!session.accountResyncArmed || session.accountResyncGeneration == 0) {
        return false;
    }
    touchesScratch = true;
    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    const std::size_t acquisitionBatch = (std::min)(session.accountAcquisitionCount,
        middleware::datagen::family4::character::layout::kInventoryChangeRecordCapacity);
    queuez::SessionState currentQueuez{};
    if (!push::append_account_resync_notification(scratch,
                                                  session.queuez,
                                                  session.credentials.bap.sessionKey,
                                                  nextSendNonce,
                                                  scratch.framed,
                                                  framedSize,
                                                  currentQueuez,
                                                  std::span(session.accountAcquisitions).first(acquisitionBatch))) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=peer_resync result=fail reason=family4");
        return false;
    }
    if (currentQueuez.family0Active) {
        queuez::SessionState appearanceAfter{};
        if (!push::append_account_resync_appearance_notification(scratch,
                                                                 currentQueuez,
                                                                 session.credentials.bap.sessionKey,
                                                                 nextSendNonce,
                                                                 scratch.framed,
                                                                 framedSize,
                                                                 appearanceAfter)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync result=fail reason=family0");
            return false;
        }
        currentQueuez = appearanceAfter;
    }
    if (currentQueuez.family3Active) {
        queuez::SessionState rosterAfter{};
        if (!push::append_account_resync_roster_notification(scratch,
                                                             currentQueuez,
                                                             session.credentials.bap.sessionKey,
                                                             nextSendNonce,
                                                             scratch.framed,
                                                             framedSize,
                                                             rosterAfter)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=queuez stage=peer_resync result=fail reason=family3");
            return false;
        }
        currentQueuez = rosterAfter;
    }
    if (framedSize == 0 || framedSize > response.size() || !queuez::valid(currentQueuez)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=peer_resync result=fail reason=output");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    session.queuez = currentQueuez;
    session.accountGeneration = session.accountResyncGeneration;
    middleware::datagen::family4::character::commit_acquisition_batch(
        session.accountAcquisitions,session.accountAcquisitionCount,acquisitionBatch);
    session.accountResyncArmed = session.accountAcquisitionCount != 0;
    session.accountStructuralChange = false;
    report_repush("peer_resync", framedSize);
    return true;
}

[[nodiscard]] bool consume_banner_repush(Session& session,
                                         Scratch& scratch,
                                         std::span<std::byte> response,
                                         std::size_t& written,
                                         bool& touchesScratch) noexcept {
    if (!session.bannerRepushArmed || session.bannerRepushRoot == 0
        || GetTickCount64() < session.bannerRepushDueTick) {
        return false;
    }
    // nothing is owed while the account has no character; keep the arm set, it's the banner's only retry
    if (state::account::banner_character_soid(state::account_snapshot()) == 0) {
        return false;
    }
    touchesScratch = true;

    // same body the subscribe answer builds; append_banner_notification pins version at zero but a
    // pick has moved past it
    middleware::queuez::Subscription subscription{};
    subscription.familyType = queuez::kBannerFamilyType;
    subscription.familyRootSoid = session.bannerRepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState bannerAfter{};
    remote_appearance::Published foreignAppearance{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     session.credentials.bap.sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     bannerAfter,
                                     armsRepush,
                                     armsBannerRepush,
                                     &foreignAppearance);
    if (framedSize == 0 || framedSize > response.size()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=banner_repush result=fail");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    if (valid(bannerAfter)) {
        session.queuez = bannerAfter;
    }
    session.bannerRepushArmed = false;
    if (foreignAppearance.valid) {
        (void)session.foreignAppearance.full(foreignAppearance, GetTickCount64());
    }
    report_repush("banner_repush", framedSize);
    return true;
}

[[nodiscard]] bool consume_foreign_appearance(Session& session,
                                              Scratch& scratch,
                                              std::span<std::byte> response,
                                              std::size_t& written,
                                              bool& touchesScratch) noexcept {
    const auto now = GetTickCount64();
    for (auto& entry : session.foreignAppearance.entries) {
        if (!remote_appearance::Tracker::due(entry, now)) {
            continue;
        }
        if (session.bannerRepushArmed && session.bannerRepushRoot == entry.state.root) {
            continue;
        }
        entry.readyAt = now + 500;
        state::AccountState remote{};
        if (entry.state.root == session.credentials.accountSoid
            || !state::social::remote_profile_cache::resolve(entry.state.root, remote)
            || remote.primarySoid != entry.state.root) {
            continue;
        }
        state::activity::ActivityState remoteActivity{};
        const state::AccountRuntimeContext context{&remote, &remoteActivity, {}, {}};
        const state::ScopedAccountRuntime scope(context);
        remote_appearance::Published published{};
        auto nextNonce = session.sendNonce;
        std::size_t size = 0;
        touchesScratch = true;
        if (!push::append_remote_appearance_notification(scratch,
                                                         entry.state,
                                                         session.credentials.bap.sessionKey,
                                                         nextNonce,
                                                         scratch.framed,
                                                         size,
                                                         published)
            || size == 0 || size > response.size()) {
            continue;
        }
        auto nextTracker = session.foreignAppearance;
        if (!nextTracker.incremental(entry.state, published)) {
            continue;
        }
        std::copy_n(scratch.framed.begin(), size, response.begin());
        written = size;
        session.sendNonce = nextNonce;
        session.foreignAppearance = nextTracker;
        report_repush("remote_appearance", size);
        return true;
    }
    return false;
}

[[nodiscard]] bool consume_social_roster_repush(Session& session,
                                                Scratch& scratch,
                                                std::span<std::byte> response,
                                                std::size_t& written,
                                                bool& touchesScratch) noexcept {
    if (!session.socialRosterRepushArmed || session.socialRosterRepushRoot == 0
        || GetTickCount64() < session.socialRosterRepushDueTick) {
        return false;
    }
    // spend the one owed attempt before encoding so a failed refresh can't starve activity keepalives
    session.socialRosterRepushArmed = false;
    touchesScratch = true;

    middleware::queuez::Subscription subscription{};
    subscription.familyType = queuez::kSocialRosterFamilyType;
    subscription.familyRootSoid = session.socialRosterRepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState after{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     session.credentials.bap.sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     after,
                                     armsRepush,
                                     armsBannerRepush);
    if (framedSize == 0 || framedSize > response.size()) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=social_roster_repush result=fail");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    if (queuez::valid(after)) {
        session.queuez = after;
    }
    report_repush("social_roster_repush", framedSize);
    return true;
}

[[nodiscard]] bool consume_social_presence(Session& session,
                                           Scratch& scratch,
                                           std::span<std::byte> response,
                                           std::size_t& written,
                                           bool& touchesScratch) noexcept {
    const auto now = GetTickCount64();
    for (auto& observation : session.socialRosterPresence.observations) {
        if (observation.root == 0 || now < observation.nextPollTick) continue;
        const auto activityIndex = roster_activity_index_locked(observation.root);
        if (!observation.changed(now, activityIndex)) continue;
        touchesScratch = true;
        middleware::queuez::Subscription subscription{};
        subscription.familyType = queuez::kSocialRosterFamilyType;
        subscription.familyRootSoid = observation.root;
        auto nextNonce = session.sendNonce;
        std::size_t framedSize = 0;
        queuez::SessionState after{};
        bool armsRepush = false;
        bool armsBannerRepush = false;
        push::append_queuez_notification(scratch, session.queuez, subscription,
                                        session.credentials.bap.sessionKey, nextNonce,
                                        scratch.framed, framedSize, after,
                                        armsRepush, armsBannerRepush);
        if (framedSize == 0 || framedSize > response.size() || !queuez::valid(after)) {
            continue;
        }
        std::copy_n(scratch.framed.begin(), framedSize, response.begin());
        written = framedSize;
        session.sendNonce = nextNonce;
        session.queuez = after;
        observation.publishedActivity = activityIndex;
        report_repush("social_presence", framedSize);
        return true;
    }
    return false;
}

// retail subscribes before the join command and may not resubscribe after an empty full snapshot;
// descriptor publication is async, so keep the subscription and retry
[[nodiscard]] bool consume_join_repush(Session& session,
                                       Scratch& scratch,
                                       std::span<std::byte> response,
                                       std::size_t& written,
                                       bool& touchesScratch) noexcept {
    if (!session.joinRepushArmed || session.joinRepushRoot == 0) {
        return false;
    }
    const std::uint64_t now = GetTickCount64();
    if (now < session.joinRepushDueTick) {
        return false;
    }
    if (session.joinRepushDeadlineTick == 0 || now >= session.joinRepushDeadlineTick) {
        session.joinRepushArmed = false;
        session.joinRepushRoot = 0;
        session.joinRepushDueTick = 0;
        session.joinRepushDeadlineTick = 0;
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=join_repush result=fail reason=timeout");
        return false;
    }

    touchesScratch = true;
    middleware::queuez::Subscription subscription{};
    subscription.familyType = middleware::datagen::kJoinFamily;
    subscription.familyRootSoid = session.joinRepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState after{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     session.credentials.bap.sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     after,
                                     armsRepush,
                                     armsBannerRepush);
    if (framedSize == 0 || framedSize > response.size()) {
        session.joinRepushDueTick = now + kJoinRepushRetryMs;
        return false;
    }

    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    if (queuez::valid(after)) {
        session.queuez = after;
    }
    session.joinRepushArmed = false;
    session.joinRepushRoot = 0;
    session.joinRepushDueTick = 0;
    session.joinRepushDeadlineTick = 0;
    report_repush("join_repush", framedSize);
    return true;
}

} // namespace

bool consume_deferred(Session& session,
                      Scratch& scratch,
                      std::span<std::byte> response,
                      std::size_t& written,
                      bool& touchesScratch) noexcept {
    written = 0;
    if (!session.authenticated) {
        return false;
    }
    // a departed public record no longer passes keepalive's State lookup; its native cleanup can't
    // wait for the five-second tick
    if (push::activity::consume_retirement(session,scratch,response,written,touchesScratch)) return true;
    if (push::activity::consume_departure_query(session,scratch,response,written,touchesScratch)) return true;
    if (consume_account_resync(session, scratch, response, written, touchesScratch)) {
        return true;
    }
    if (session.accountResyncArmed) {
        return false;
    }
    if (!session.family4RepushArmed || session.family4RepushRoot == 0
        || GetTickCount64() < session.family4RepushDueTick) {
        return consume_join_repush(session, scratch, response, written, touchesScratch)
               || consume_social_roster_repush(session, scratch, response, written, touchesScratch)
               || consume_social_presence(session, scratch, response, written, touchesScratch)
               || consume_banner_repush(session, scratch, response, written, touchesScratch)
               || consume_foreign_appearance(session, scratch, response, written, touchesScratch)
               || push::activity::consume_activity_keepalive(
                   session, scratch, response, written, touchesScratch);
    }
    touchesScratch = true;

    middleware::queuez::Subscription subscription{};
    subscription.familyType = queuez::kAccountFamilyType;
    subscription.familyRootSoid = session.family4RepushRoot;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    queuez::SessionState after{};
    bool armsRepush = false;
    bool armsBannerRepush = false;
    push::append_queuez_notification(scratch,
                                     session.queuez,
                                     subscription,
                                     session.credentials.bap.sessionKey,
                                     nextSendNonce,
                                     scratch.framed,
                                     framedSize,
                                     after,
                                     armsRepush,
                                     armsBannerRepush);
    if (framedSize == 0 || framedSize > response.size()) {
        // neither failure clears on retry; holding the arm starves the keepalive and the client drops
        // the activity session
        session.family4RepushArmed = false;
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         framedSize == 0 ? "ev=queuez stage=repush result=fail reason=encode"
                                         : "ev=queuez stage=repush result=fail reason=capacity");
        return false;
    }
    std::copy_n(scratch.framed.begin(), framedSize, response.begin());
    written = framedSize;
    session.sendNonce = nextSendNonce;
    if (queuez::valid(after)) {
        session.queuez = after;
    }
    session.family4RepushArmed = false;
    report_repush("repush", framedSize);
    return true;
}

bool weapon_acquisition_push_self_test(std::uint64_t characterSoid,
    std::uint64_t instanceSoid, void* compression) noexcept {
    namespace native=middleware::datagen::family4::character;
    const auto account=state::account_snapshot();
    if (!compression || !characterSoid || !instanceSoid || !account.primarySoid) return false;
    static Scratch scratch{};
    static push::snapshot::Prepared prepared{};
    prepared.family={}; prepared.objects={}; prepared.rawClearSize=prepared.compressedClearSize=0;
    SecureZeroMemory(&scratch,sizeof(scratch));
    const std::array<native::AcquisitionNotice,1> notice{{{characterSoid,instanceSoid}}};
    if (!push::snapshot::prepare_family4_refresh(scratch,account.primarySoid,1,prepared,notice)) return false;
    const auto objects=prepared.family.objects;
    std::size_t itemIndex=objects.size(), characterIndex=objects.size();
    for (std::size_t i=0;i<objects.size();++i) {
        if (objects[i].version==instanceSoid) itemIndex=i;
        if (objects[i].version==characterSoid) characterIndex=i;
    }
    if (itemIndex>=characterIndex || characterIndex>=objects.size()) return false;
    native::layout::Object decoded{};
    auto bytes=std::span<std::byte>(reinterpret_cast<std::byte*>(&decoded),sizeof(decoded));
    if (!middleware::compression::oodle::decompress(static_cast<HMODULE>(compression),objects[characterIndex].payload,bytes)) return false;
    const auto row=std::find_if(decoded.inventoryItems.begin(),decoded.inventoryItems.end(),
        [&](const auto& value) { return value.instanceSoid==instanceSoid; });
    if (row==decoded.inventoryItems.end() || decoded.inventoryChanges.nextSequence!=1
        || decoded.inventoryChanges.records[0].kind!=1 || decoded.inventoryChanges.records[0].flags!=0
        || decoded.inventoryChanges.records[0].mutationSerial!=row->mutationSerial) return false;
    static Session session{};
    SecureZeroMemory(&session,sizeof(session));
    session.authenticated=true; session.credentials.accountSoid=account.primarySoid;
    session.queuez.family4Active=true; session.queuez.family4RootSoid=account.primarySoid;
    session.queuez.family4ResidentCount=1;
    session.queuez.family4Residents[0]={account.primarySoid,middleware::datagen::kAccountObjectId};
    session.accountResyncArmed=true; session.accountResyncGeneration=1;
    session.accountAcquisitionCount=native::layout::kInventoryChangeRecordCapacity+1;
    std::fill_n(session.accountAcquisitions.begin(),session.accountAcquisitionCount,notice[0]);
    std::array<std::byte,1> tooSmall{};
    std::size_t written=0; bool touched=false;
    if (consume_account_resync(session,scratch,tooSmall,written,touched)
        || session.accountAcquisitionCount!=17 || !session.accountResyncArmed
        || session.queuez.family4Version!=0 || session.sendNonce!=std::array<std::byte,state::kBapNonceSize>{}) return false;
    static std::array<std::byte,client::network::kBapFrameCapacity> response{};
    if (!consume_account_resync(session,scratch,response,written,touched) || !written
        || session.accountAcquisitionCount!=1 || !session.accountResyncArmed
        || session.queuez.family4Version!=1) return false;
    if (!consume_account_resync(session,scratch,response,written,touched) || !written
        || session.accountAcquisitionCount!=0 || session.accountResyncArmed
        || session.queuez.family4Version!=2) return false;
    if (consume_account_resync(session,scratch,response,written,touched)) return false;
    return true;
}

} // namespace dawn::server::bap::encrypted
