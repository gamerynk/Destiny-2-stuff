#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <limits>

#include "../../core/logging/log.h"
#include "../../state/activity/destination/activity_destination_snapshot.h"
#include "../../state/matchmaking/matchmaking_state.h"
#include "../../state/runtime/runtime.h"
#include "../../state/activity/shared/runtime.h"
#include "../../state/activity/entity_retirement/runtime.h"
#include "../../state/activity/runtime.h"
#include "../../state/build_data/runtime.h"
#include "internal.h"
#include "runtime.h"
#include "encrypted/push/activity/activity_roster_push.h"

namespace dawn::server::bap {
namespace {

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Session, kSessionCount> g_sessions{};
Scratch g_scratch{};
std::uint64_t g_accountGeneration{};
std::uint64_t g_connectionIncarnation{};
thread_local bool g_dispatchOwnsLock{};
struct DispatchOwnership final {
    bool previous{g_dispatchOwnsLock};
    DispatchOwnership() noexcept { g_dispatchOwnsLock=true; }
    ~DispatchOwnership() noexcept { g_dispatchOwnsLock=previous; }
};

void publish_account_mutation(Session& origin) noexcept {
    origin.accountMutationPublished = false;
    g_accountGeneration = g_accountGeneration == (std::numeric_limits<std::uint64_t>::max)()
                              ? 1
                              : g_accountGeneration + 1;
    origin.accountGeneration = g_accountGeneration;
    origin.accountResyncGeneration = g_accountGeneration;
    // a structural change keeps the origin armed; its reply did not carry the full state
    origin.accountResyncArmed = origin.accountStructuralChange || origin.accountAcquisitionCount != 0;
    origin.accountStructuralChange = false;
    std::size_t armed = 0;
    for (auto& peer : g_sessions) {
        if (&peer != &origin && peer.id != 0 && peer.authenticated) {
            peer.foreignAppearance.changed(origin.credentials.accountSoid);
        }
        if (&peer == &origin || peer.id == 0 || !peer.authenticated || !peer.queuez.family4Active
            || peer.credentials.accountSoid != origin.credentials.accountSoid) {
            continue;
        }
        peer.accountResyncGeneration = g_accountGeneration;
        peer.accountResyncArmed = true;
        ++armed;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=queuez stage=peer_resync_arm result=ok generation=%llu "
                                    "origin=%u peers=%zu",
                                    static_cast<unsigned long long>(g_accountGeneration),
                                    origin.id,
                                    armed);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

[[nodiscard]] Session* session_for(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size()) {
        return nullptr;
    }
    auto& session = g_sessions[id - 1];
    return session.id == id ? &session : nullptr;
}

void clear_session(Session& session) noexcept {
    SecureZeroMemory(&session, sizeof session);
}

[[nodiscard]] bool release_matchmaking_context(Session& session) noexcept {
    if (session.matchmakingContext.generation == state::matchmaking::kInvalidGeneration) {
        session.matchmakingContext = {};
        return true;
    }
    if (!state::matchmaking::release_context(session.matchmakingContext)) {
        return false;
    }
    session.matchmakingContext = {};
    return true;
}

[[nodiscard]] bool open_session(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size() || g_connectionIncarnation==(std::numeric_limits<std::uint64_t>::max)()) {
        return false;
    }
    auto& session = g_sessions[id - 1];
    if (session.id != 0 && !release_matchmaking_context(session)) {
        return false;
    }
    clear_session(session);
    session.id = id;
    session.connectionIncarnation=++g_connectionIncarnation;
    session.openedAtTick = GetTickCount64();
    return true;
}

[[nodiscard]] bool close_session(std::uint32_t id) noexcept {
    if (id == 0 || id > g_sessions.size()) {
        return false;
    }
    auto& session = g_sessions[id - 1];
    if (session.id != 0 && !release_matchmaking_context(session)) {
        return false;
    }
    state::activity::shared::disconnect(session.accountRuntime,session.activitySessionId,
                                        session.activityRetirementJoinRevision);
    if (session.authenticated && state::activity::entity_retirement::close_view(
        session.activitySessionId,session.credentials.accountSoid,session.activityRetirementJoinRevision))
        core::log::write(core::log::Channel::server,core::log::Level::info,
                        "ev=activity stage=departure_cleanup result=armed source=closed_view");
    clear_session(session);
    return true;
}

[[nodiscard]] bool consume_frame(const client::network::BapRequest& request,
                                 client::network::BapResponse& response) noexcept {
    middleware::bap::OuterFrame frame;
    if (!middleware::bap::parse_frame(request.frame, frame)) {
        return false;
    }
    auto* session = session_for(request.connectionId);
    if (session == nullptr) {
        return false;
    }
    const state::ScopedAccountRuntime accountScope(session->accountRuntime);
    session->lastFrameAtTick = GetTickCount64();
    bool handled = false;
    if (frame.frameType == middleware::bap::FrameType::encrypted) {
        handled = encrypted::consume(*session, g_scratch, frame, request.response, response.size);
    } else {
        handled = plaintext::consume(*session, g_scratch, frame, request.response, response.size);
    }
    if (!handled) {
        return false;
    }
    if (frame.frameType == middleware::bap::FrameType::encrypted
        && session->accountMutationPublished) {
        publish_account_mutation(*session);
    }
    bool touchesScratch = true;
    std::size_t deferred = 0;
    if (response.size < request.response.size()
        && encrypted::consume_deferred(*session,
                                       g_scratch,
                                       request.response.subspan(response.size),
                                       deferred,
                                       touchesScratch)) {
        response.size += deferred;
    }
    return true;
}

[[nodiscard]] bool consume_poll(const client::network::BapRequest& request,
                                client::network::BapResponse& response,
                                bool& touchesScratch) noexcept {
    static std::atomic_bool reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(
            core::log::Channel::server, core::log::Level::info, "ev=queuez stage=poll result=ok");
    }
    auto* session = session_for(request.connectionId);
    if (session == nullptr) {
        return false;
    }
    const state::ScopedAccountRuntime accountScope(session->accountRuntime);
    return encrypted::consume_deferred(
        *session, g_scratch, request.response, response.size, touchesScratch);
}

} // namespace

// caller holds the BAP lock, or runs in startup preflight before any connection exists
std::int16_t roster_activity_index_locked(std::uint64_t accountSoid) noexcept {
    social_roster_presence::ActivityChoice choice{};
    for (const auto& session : g_sessions) {
        if (!social_roster_presence::eligible(accountSoid, session.credentials.accountSoid,
                                              session.authenticated, session.id != 0,
                                              session.activityContract.joined,
                                              session.activityJoinedForeignSession)
            || !session.socialRosterArrival.ready(session.activitySessionId)) {
            continue;
        }
        state::activity::destination::DestinationSelection selection{};
        if (state::activity::destination::snapshot_for_runtime(
                session.accountRuntime, session.credentials.accountSoid,
                session.activitySessionId, selection)) {
            choice.consider(session.openedAtTick, selection.activityIndex);
        }
    }
    return choice.index;
}

bool roster_presence_self_test(std::uint64_t ownerActivitySessionId) noexcept {
    const auto owner = state::account_snapshot();
    state::activity::destination::DestinationSelection destination{};
    if (!state::activity::destination::snapshot_for_runtime(
            {}, owner.primarySoid, ownerActivitySessionId, destination)
        || destination.activityIndex == 0) return false;

    AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    for (const auto& session : g_sessions) {
        if (session.id != 0) {
            ReleaseSRWLockExclusive(&g_lock);
            return false;
        }
    }
    auto& local = g_sessions[0];
    auto& remote = g_sessions[1];
    clear_session(local);
    clear_session(remote);
    local.id = 1;
    local.authenticated = true;
    local.credentials.accountSoid = owner.primarySoid;
    local.activitySessionId = ownerActivitySessionId;
    local.activityContract.joined = true;
    local.openedAtTick = 1;
    bool success = roster_activity_index_locked(owner.primarySoid) == 0;

    local.activityRosterClientSyncKnown = true;
    local.activityRosterAwaitClientSync = true;
    local.activityRosterStaged.staged = true;
    encrypted::push::activity::commit_staged_roster(local);
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    local.activityRosterAwaitClientSync = false;
    local.activityRosterStaged.priorClientSyncKnown = true;
    local.activityRosterStaged.priorAwaitClientSync = true;
    local.activityRosterStaged.staged = true;
    encrypted::push::activity::discard_staged_roster(local);
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    local.activityRosterAwaitClientSync = false;
    local.activityRosterStaged.staged = true;
    encrypted::push::activity::commit_staged_roster(local);
    success = success && roster_activity_index_locked(owner.primarySoid)
                             == destination.activityIndex;
    local.activityRosterAwaitClientSync = true;
    local.activityRosterStaged.staged = true;
    encrypted::push::activity::commit_staged_roster(local);
    success = success && roster_activity_index_locked(owner.primarySoid)
                             == destination.activityIndex;
    local.activityJoinedForeignSession = true;
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    local.activityJoinedForeignSession = false;
    local.activityContract.joined = false;
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    local.activityContract.joined = true;
    local.authenticated = false;
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    local.authenticated = true;

    // empty runtime is the process owner, even inside another account's TLS scope
    static state::AccountState remoteAccount{};
    static state::activity::ActivityState remoteActivity{};
    remoteAccount = owner;
    remoteAccount.primarySoid ^= 0x100000000ULL;
    if (remoteAccount.primarySoid == 0) remoteAccount.primarySoid = 1;
    remoteActivity = {};
    auto& record = remoteActivity.sessions[0];
    record.occupied = true;
    record.ownerAccountSoid = remoteAccount.primarySoid;
    record.sessionId = ownerActivitySessionId + 1;
    record.destination = destination;
    record.destination.activityIndex = destination.activityIndex == 14 ? 8 : 14;
    const state::AccountRuntimeContext remoteRuntime{&remoteAccount, &remoteActivity};
    remote.id = 2;
    remote.authenticated = true;
    remote.credentials.accountSoid = remoteAccount.primarySoid;
    remote.accountRuntime = remoteRuntime;
    remote.activitySessionId = record.sessionId;
    remote.activityContract.joined = true;
    remote.openedAtTick = 2;
    remote.socialRosterArrival.note_delivered(record.sessionId, true, true, false);
    {
        const state::ScopedAccountRuntime scope(remoteRuntime);
        success = success && roster_activity_index_locked(owner.primarySoid)
                                 == destination.activityIndex
                          && roster_activity_index_locked(remoteAccount.primarySoid)
                                 == record.destination.activityIndex;
        state::activity::destination::DestinationSelection rejected{};
        success = success && !state::activity::destination::snapshot_for_runtime(
                                 remoteRuntime, owner.primarySoid, record.sessionId, rejected)
                          && !state::activity::destination::snapshot_for_runtime(
                                 {&remoteAccount, nullptr}, remoteAccount.primarySoid,
                                 record.sessionId, rejected);
    }
    local.id = 0;
    success = success && roster_activity_index_locked(owner.primarySoid) == 0;
    clear_session(local);
    clear_session(remote);
    SecureZeroMemory(&remoteAccount, sizeof remoteAccount);
    SecureZeroMemory(&remoteActivity, sizeof remoteActivity);
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool consume(const client::network::BapRequest& request,
             client::network::BapResponse& response) noexcept {
    response = {};
    AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    bool success = false;
    bool touchesScratch = request.event != client::network::BapEvent::poll;
    // hold the session lock across crypto counter reads and updates
    switch (request.event) {
    case client::network::BapEvent::open:
        success = open_session(request.connectionId);
        break;
    case client::network::BapEvent::frame:
        success = consume_frame(request, response);
        break;
    case client::network::BapEvent::poll:
        success = consume_poll(request, response, touchesScratch);
        break;
    case client::network::BapEvent::close:
        success = close_session(request.connectionId);
        break;
    }
    // decrypted frames can hold runtime-only keys or tokens, so scratch never outlives the call
    if (touchesScratch) {
        SecureZeroMemory(&g_scratch, sizeof g_scratch);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

std::size_t snapshot_connections(std::span<ConnectionStatus> output) noexcept {
    AcquireSRWLockShared(&g_lock);
    std::size_t count = 0;
    for (const Session& session : g_sessions) {
        if (session.id == 0 || count == output.size()) {
            continue;
        }
        ConnectionStatus& status = output[count++];
        status = {};
        status.accountSoid = session.authenticated ? session.credentials.accountSoid : 0;
        status.characterSoid = session.activityCharacterSoid;
        status.activitySessionId = session.activitySessionId;
        status.openedAtTick = session.openedAtTick;
        status.lastFrameAtTick = session.lastFrameAtTick;
        status.connectionId = session.id;
        status.authenticated = session.authenticated;
        status.activityJoined = session.activityContract.joined;
    }
    ReleaseSRWLockShared(&g_lock);
    return count;
}

bool snapshot_native_activity_views(std::uint64_t account,
    std::span<NativeActivityView> output, std::size_t& count) noexcept {
    count = 0;
    if (!account) return false;
    struct Request {
        std::uint64_t account;
        std::span<NativeActivityView> output;
        std::size_t count{};
    } request{account, output};
    const bool ok = with_account_transaction([](void* opaque) noexcept {
        auto& query = *static_cast<Request*>(opaque);
        for (const auto& session : g_sessions) {
            if (!session.id || !session.authenticated || !session.connectionIncarnation
                || session.credentials.accountSoid != query.account
                || !session.activityContract.joined || !session.activityPatchEpochSeen
                || !session.activitySessionId || !session.activityMemberKey) continue;
            state::activity::SessionRecord record{};
            if (!state::activity::snapshot_for_runtime(session.accountRuntime, query.account,
                    session.activitySessionId, record)
                || !record.joined || record.restoredDormant
                || record.memberKey != session.activityMemberKey) return false;
            if (query.count == query.output.size()) return false;
            for (std::size_t i = 0; i < query.count; ++i)
                if (query.output[i].activity == session.activitySessionId) return false;
            query.output[query.count++] = {query.account, session.activitySessionId,
                session.activityMemberKey, session.connectionIncarnation, record.createdRevision,
                record.joinedRevision, session.activityRetirementJoinRevision,
                {session.activityPatchEpoch.first, session.activityPatchEpoch.second}, session.id,
                session.activityPublishedRetirementSerial};
            auto& view = query.output[query.count - 1];
            view.serverEntitySlots = record.serverEntitySlots;
            view.publicWorld = record.publicWorld;
        }
        return true;
    }, &request);
    if (!ok) {
        for (auto& row : output) row = {};
        return false;
    }
    count = request.count;
    return true;
}

bool snapshot_native_membership_views(std::uint64_t account,
    std::span<NativeActivityView> output, std::size_t& count) noexcept {
    count = 0;
    if (!account) return false;
    struct Request {
        std::uint64_t account;
        std::span<NativeActivityView> output;
        std::size_t count{};
    } request{account, output};
    const bool ok = with_account_transaction([](void* opaque) noexcept {
        auto& query = *static_cast<Request*>(opaque);
        for (const auto& session : g_sessions) {
            if (!session.id || !session.authenticated || !session.connectionIncarnation
                || session.credentials.accountSoid != query.account
                || !session.activityContract.joined
                || !session.activitySessionId || !session.activityMemberKey) continue;
            state::activity::SessionRecord record{};
            if (!state::activity::snapshot_for_runtime(session.accountRuntime, query.account,
                    session.activitySessionId, record)
                || !record.joined || record.restoredDormant
                || record.memberKey != session.activityMemberKey) return false;
            if (query.count == query.output.size()) return false;
            for (std::size_t i = 0; i < query.count; ++i)
                if (query.output[i].activity == session.activitySessionId) return false;
            query.output[query.count++] = {query.account, session.activitySessionId,
                session.activityMemberKey, session.connectionIncarnation, record.createdRevision,
                record.joinedRevision, session.activityRetirementJoinRevision,
                {0,0}, session.id,
                session.activityPublishedRetirementSerial};
            auto& view = query.output[query.count - 1];
            view.serverEntitySlots = record.serverEntitySlots;
            view.publicWorld = record.publicWorld;
        }
        return true;
    }, &request);
    if (!ok) {
        for (auto& row : output) row = {};
        return false;
    }
    count = request.count;
    return true;
}

void social_projection_changed() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    const std::uint64_t due = GetTickCount64() + 50U;
    for (Session& session : g_sessions) {
        if (session.id == 0 || !session.authenticated
            || session.socialRosterRepushRoot == 0) {
            continue;
        }
        session.socialRosterRepushDueTick = due;
        session.socialRosterRepushArmed = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void account_roster_changed(std::uint64_t accountSoid) noexcept {
    if (accountSoid == 0) {
        return;
    }
    std::uint64_t generation = 0;
    std::size_t armed = 0;
    const bool external = !g_dispatchOwnsLock;
    if (external) AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    g_accountGeneration = g_accountGeneration == (std::numeric_limits<std::uint64_t>::max)()
                              ? 1
                              : g_accountGeneration + 1;
    generation = g_accountGeneration;
    for (Session& session : g_sessions) {
        if (session.id == 0 || !session.authenticated || !session.queuez.family4Active
            || session.credentials.accountSoid != accountSoid) {
            continue;
        }
        session.accountResyncGeneration = g_accountGeneration;
        session.accountResyncArmed = true;
        session.accountStructuralChange = true;
        ++armed;
    }
    if (external) ReleaseSRWLockExclusive(&g_lock);
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=queuez stage=account_roster_changed result=ok generation=%llu "
                                    "account=0x%016llX peers=%zu",
                                    static_cast<unsigned long long>(generation),
                                    static_cast<unsigned long long>(accountSoid),
                                    armed);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

bool with_account_transaction(bool (*operation)(void*) noexcept, void* context) noexcept {
    if (!operation) return false;
    if (g_dispatchOwnsLock) return operation(context);
    AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    const bool result = operation(context);
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

bool inventory_feedback_publication_self_test() noexcept {
    return with_account_transaction([](void*) noexcept {
        for (const auto& session : g_sessions) if (session.id) return false;
        Session origin{};
        origin.accountAcquisitionCount=1;
        origin.accountResyncArmed=true;
        publish_account_mutation(origin);
        return origin.accountResyncArmed && origin.accountAcquisitionCount==1;
    },nullptr);
}

void account_inventory_acquired(std::uint64_t accountSoid, std::uint64_t characterSoid,
                                std::uint64_t instanceSoid) noexcept {
    struct Context { std::uint64_t account, character, instance; } context{accountSoid,characterSoid,instanceSoid};
    (void)with_account_transaction([](void* value) noexcept {
        const auto& notice=*static_cast<Context*>(value);
        if (!notice.account || !notice.character || !notice.instance) return false;
        account_roster_changed(notice.account);
        for (auto& session : g_sessions) {
            if (!session.id || !session.authenticated || !session.queuez.family4Active
                || session.credentials.accountSoid!=notice.account) continue;
            const state::ScopedAccountRuntime accountScope(session.accountRuntime);
            const auto account=state::account_snapshot();
            std::size_t retained=0;
            for (std::size_t index=0;index<session.accountAcquisitionCount;++index) {
                const auto pending=session.accountAcquisitions[index];
                bool present=false;
                for (std::size_t character=0;character<account.characterCount;++character) {
                    const auto& owner=account.characters[character];
                    if (owner.soid!=pending.characterSoid) continue;
                    for (std::size_t item=0;item<owner.inventory.count;++item)
                        present=present || owner.inventory.values[item].instanceSoid==pending.instanceSoid;
                    for (const auto& item : owner.equipment.slots)
                        present=present || (item && item->instanceSoid==pending.instanceSoid);
                }
                if (present) session.accountAcquisitions[retained++]=pending;
            }
            session.accountAcquisitionCount=retained;
            if (session.accountAcquisitionCount==session.accountAcquisitions.size()) {
                core::log::write(core::log::Channel::server,core::log::Level::warn,
                    "ev=queuez stage=acquire_feedback result=full");
                continue;
            }
            session.accountAcquisitions[session.accountAcquisitionCount++]={notice.character,notice.instance};
        }
        return true;
    },&context);
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const DispatchOwnership ownership;
    for (auto& session : g_sessions) {
        state::activity::shared::disconnect(session.accountRuntime,session.activitySessionId);
        if (session.id != 0
            && session.matchmakingContext.generation != state::matchmaking::kInvalidGeneration) {
            (void)state::matchmaking::release_context(session.matchmakingContext);
        }
    }
    SecureZeroMemory(g_sessions.data(), sizeof g_sessions);
    SecureZeroMemory(&g_scratch, sizeof g_scratch);
    g_accountGeneration = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::server::bap
