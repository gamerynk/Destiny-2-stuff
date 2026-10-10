#include "realtime_client.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../../client/hooks/external_server/transport.h"
#include "../../../client/crucible/match_panel.h"
#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../core/logging/log.h"
#include "../../../core/runtime/runtime_policy.h"
#include "../../../core/ui/notice/ui_notice_overlay.h"
#include "../../../middleware/crypto/lookup3.h"
#include "../../../middleware/social/membership_projection.h"
#include "../../../middleware/social/rendezvous_batch.h"
#include "../runtime.h"
#include "../native_network_diagnostics.h"
#include "central_gameplay_bootstrap.h"
#include "initial_lobby_bootstrap.h"
#include "lobby_cache.h"
#include "owner_gameplay_admission.h"
#include "presence_outbox.h"
#include "rendezvous_sender.h"

namespace dawn::steam::runtime::social::realtime_client {
namespace {

constexpr std::uint64_t kPollIntervalMilliseconds = 1000;
constexpr std::uint64_t kRetryIntervalMilliseconds = 5000;
constexpr int kResultOk = 1;
constexpr int kGameRichPresenceJoinRequestedCallback = 337;
constexpr int kLobbyDataUpdateCallback = 505;
constexpr int kLobbyChatUpdateCallback = 506;
constexpr int kLobbyChatMessageCallback = 507;
constexpr std::size_t kChatCapacity = 32;
constexpr std::uint64_t kOwnerAdmissionRetryMilliseconds = 1000;
constexpr char kOwnerAdmissionDiagnosticKey[] = "__dawn_owner_admission_diag_v1";
constexpr std::size_t kOwnerAdmissionDiagnosticCapacity = 16;
constexpr std::uint64_t kMembershipProjectionRetryMilliseconds = 250;
// the host's superseded native session is retired by a later retail pump, not inline with
// the membership apply, so keep one same-lobby owner leave suppressible for that window
constexpr std::uint64_t kMembershipProjectionLeaveSuppressionMilliseconds = 5000;
constexpr std::uintptr_t kPrimaryGroupSessionAccessorRva = 0x00C26490U;
constexpr std::uintptr_t kMembershipApplyWrapperRva = 0x01781790U;
constexpr std::size_t kSessionMembershipStateOffset = 0x0868U;
constexpr std::size_t kSessionMembershipRevisionOffset = 0x086CU;
constexpr std::size_t kSessionModeOffset = 0x1AEF8U;
constexpr std::int32_t kPeerEstablishedMode = 4;
constexpr std::int32_t kHostEstablishedMode = 6;
constexpr std::uint32_t kNativeSessionHashInitial = 0xDEAE2F4EU;

static_assert(sizeof(kOwnerAdmissionDiagnosticKey) <= wire::kKeyCapacity);

struct GameRichPresenceJoinRequested final {
    std::uint64_t friendSteamId{};
    std::array<char, 256> connect{};
};

struct LobbyDataUpdate final {
    std::uint64_t lobby{};
    std::uint64_t member{};
    std::uint8_t success{};
    std::array<std::byte, 7> padding{};
};

struct LobbyChatUpdate final {
    std::uint64_t lobby{};
    std::uint64_t userChanged{};
    std::uint64_t makingChange{};
    std::uint32_t stateChange{};
    std::uint32_t padding{};
};

struct LobbyChatMessage final {
    std::uint64_t lobby{};
    std::uint64_t sender{};
    std::uint8_t entryType{};
    std::array<std::byte, 3> padding{};
    std::int32_t messageId{};
};

struct ChatRecord final {
    std::uint64_t lobby{};
    std::uint64_t sender{};
    std::int32_t id{};
    std::array<std::byte, wire::kEventPayloadCapacity> payload{};
    std::size_t payloadSize{};
    bool occupied{};
};

struct PendingOwnerGameplayAdmission final {
    wire::Event event{};
    std::uint64_t nextAttempt{};
    bool active{};
};

struct InternalOwnerTransition final {
    std::uint64_t lobbyId{};
    std::uint64_t sequence{};
    std::uint64_t suppressLeaveUntil{};
    bool suppressLeavePending{};
};

struct OwnerGameplayAdmissionDiagnostic final {
    std::uint64_t revision{};
    std::uint64_t sequence{};
    std::uint64_t lobbyId{};
    std::uint64_t sourcePlatformId{};
    std::size_t payloadSize{};
    std::uint32_t registrations{};
    std::array<char, 24> state{};
    std::array<char, 64> reason{};
};

struct QueuedOwnerGameplayAdmission final {
    wire::Event event{};
    std::size_t connectSize{};
    bool active{};
};

struct PendingMembershipProjectionCapture final {
    std::array<std::byte, middleware::social::membership_projection::kDecodedSize> decoded{};
    std::uint32_t revision{};
    bool active{};
};

struct PendingMembershipProjectionUpload final {
    std::array<std::byte, wire::kEventPayloadCapacity> payload{};
    std::size_t payloadSize{};
    std::uint32_t revision{};
    bool active{};
};

struct PendingHostMembershipProjection final {
    wire::Event event{};
    std::uint64_t nextAttempt{};
    bool active{};
};

struct NativeFireteamDescriptorPublication final {
    std::uint64_t lobbyId{};
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize> descriptor{};
    bool active{};
};

static_assert(sizeof(GameRichPresenceJoinRequested) == 264);
static_assert(sizeof(LobbyDataUpdate) == 24);
static_assert(sizeof(LobbyChatUpdate) == 32);
static_assert(sizeof(LobbyChatMessage) == 24);

SRWLOCK g_stateLock{SRWLOCK_INIT};
SRWLOCK g_transportLock{SRWLOCK_INIT};
wire::Lobby g_lobby{};
lobby_cache::Cache g_lobbies{};
std::uint64_t g_cursor{};
std::array<ChatRecord, kChatCapacity> g_chat{};
PendingInvite g_pendingInvite{};
CentralGameplayBootstrapSnapshot g_centralGameplayBootstrap{};
SilentOwnerGameplayAdmissionSnapshot g_silentOwnerGameplayAdmission{};
InternalOwnerTransition g_internalOwnerTransition{};
// membership projection can schedule a later same-lobby native cleanup; its one-shot guard
// is separate from owner admission so it cannot consume more than one leave
InternalOwnerTransition g_membershipProjectionOwnerTransition{};
std::int32_t g_nextChatId{1};
HANDLE g_worker{};
bool g_accepting{};
bool g_lastPollSucceeded{true};
std::uint64_t g_nextPoll{};
std::uint64_t g_lastOwnerGameplayAdmissionSequence{};
PendingOwnerGameplayAdmission g_pendingOwnerGameplayAdmission{};
QueuedOwnerGameplayAdmission g_queuedOwnerGameplayAdmission{};
QueuedOwnerGameplayAdmission g_dispatchedOwnerGameplayAdmission{};
std::array<OwnerGameplayAdmissionDiagnostic, kOwnerAdmissionDiagnosticCapacity>
    g_ownerAdmissionDiagnostics{};
std::size_t g_ownerAdmissionDiagnosticHead{};
std::size_t g_ownerAdmissionDiagnosticCount{};
std::uint64_t g_nextOwnerAdmissionDiagnosticRevision{1};
OwnerGameplayAdmissionDiagnostic g_lastOwnerAdmissionDiagnostic{};
bool g_haveLastOwnerAdmissionDiagnostic{};
PendingMembershipProjectionCapture g_pendingMembershipProjectionCapture{};
PendingMembershipProjectionUpload g_pendingMembershipProjectionUpload{};
PendingHostMembershipProjection g_pendingHostMembershipProjection{};
std::uint64_t g_lastHostMembershipProjectionSequence{};
NativeFireteamDescriptorPublication g_pendingNativeFireteamDescriptor{};
NativeFireteamDescriptorPublication g_publishedNativeFireteamDescriptor{};
presence_outbox::Outbox g_presenceOutbox{};

template <std::size_t Capacity>
void assign_diagnostic_text(std::array<char, Capacity>& destination,
                            const char* source) noexcept {
    destination = {};
    if (source == nullptr) {
        return;
    }
    const std::size_t copied = (std::min)(std::strlen(source), Capacity - 1U);
    std::copy_n(source, copied, destination.begin());
}

[[nodiscard]] bool same_diagnostic(const OwnerGameplayAdmissionDiagnostic& left,
                                   const OwnerGameplayAdmissionDiagnostic& right) noexcept {
    return left.sequence == right.sequence && left.lobbyId == right.lobbyId
           && left.sourcePlatformId == right.sourcePlatformId
           && left.payloadSize == right.payloadSize
           && left.registrations == right.registrations
           && std::strcmp(left.state.data(), right.state.data()) == 0
           && std::strcmp(left.reason.data(), right.reason.data()) == 0;
}

void retain_owner_gameplay_admission_diagnostic(const wire::Event& event,
                                                const char* state,
                                                const char* reason,
                                                std::size_t registrations) noexcept {
    OwnerGameplayAdmissionDiagnostic diagnostic{};
    diagnostic.sequence = event.sequence;
    diagnostic.lobbyId = event.lobbyId;
    diagnostic.sourcePlatformId = event.sourcePlatformId;
    diagnostic.payloadSize = event.payloadSize;
    diagnostic.registrations = static_cast<std::uint32_t>(
        (std::min)(registrations,
                   static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())));
    assign_diagnostic_text(diagnostic.state, state);
    assign_diagnostic_text(diagnostic.reason, reason);

    AcquireSRWLockExclusive(&g_stateLock);
    if (g_haveLastOwnerAdmissionDiagnostic
        && same_diagnostic(g_lastOwnerAdmissionDiagnostic, diagnostic)) {
        ReleaseSRWLockExclusive(&g_stateLock);
        return;
    }
    diagnostic.revision = g_nextOwnerAdmissionDiagnosticRevision++;
    if (g_nextOwnerAdmissionDiagnosticRevision == 0) {
        g_nextOwnerAdmissionDiagnosticRevision = 1;
    }
    if (g_ownerAdmissionDiagnosticCount == g_ownerAdmissionDiagnostics.size()) {
        g_ownerAdmissionDiagnostics[g_ownerAdmissionDiagnosticHead] = {};
        g_ownerAdmissionDiagnosticHead =
            (g_ownerAdmissionDiagnosticHead + 1U) % g_ownerAdmissionDiagnostics.size();
        --g_ownerAdmissionDiagnosticCount;
    }
    const std::size_t tail =
        (g_ownerAdmissionDiagnosticHead + g_ownerAdmissionDiagnosticCount)
        % g_ownerAdmissionDiagnostics.size();
    g_ownerAdmissionDiagnostics[tail] = diagnostic;
    ++g_ownerAdmissionDiagnosticCount;
    g_lastOwnerAdmissionDiagnostic = diagnostic;
    g_haveLastOwnerAdmissionDiagnostic = true;
    ReleaseSRWLockExclusive(&g_stateLock);
}

[[nodiscard]] bool attach_owner_gameplay_admission_diagnostic(
    const OwnerGameplayAdmissionDiagnostic& diagnostic,
    wire::Request& request) noexcept {
    request.key = {};
    request.value = {};
    std::copy_n(kOwnerAdmissionDiagnosticKey,
                sizeof(kOwnerAdmissionDiagnosticKey),
                request.key.begin());
    const int written = std::snprintf(
        request.value.data(),
        request.value.size(),
        "revision=%llu state=%s reason=%s registrations=%u sequence=%llu"
        " lobby=%016llX source=%016llX payload_bytes=%zu",
        static_cast<unsigned long long>(diagnostic.revision),
        diagnostic.state.data(),
        diagnostic.reason.data(),
        static_cast<unsigned>(diagnostic.registrations),
        static_cast<unsigned long long>(diagnostic.sequence),
        static_cast<unsigned long long>(diagnostic.lobbyId),
        static_cast<unsigned long long>(diagnostic.sourcePlatformId),
        diagnostic.payloadSize);
    return written > 0 && static_cast<std::size_t>(written) < request.value.size();
}

[[nodiscard]] bool transport(wire::Request request, wire::Response& response) noexcept {
    std::array<std::byte,
               core::identity::device_identity::kProofSize + wire::kMaximumRequestSize>
        requestStorage{};
    std::array<std::byte, wire::kMaximumResponseSize> responseStorage{};
    std::size_t requestSize = 0;
    const auto nowValue = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    std::size_t proofSize = 0;
    bool identityReady = false;
    if constexpr (steam::kUniversalClientBuild) {
        proofSize = core::identity::device_identity::kProofSize;
        identityReady = nowValue > 0
                        && core::identity::device_identity::issue(
                            core::identity::device_identity::Scope::socialRealtime,
                            static_cast<std::uint64_t>(nowValue),
                            std::span<std::byte, core::identity::device_identity::kProofSize>(
                                requestStorage.data(),
                                core::identity::device_identity::kProofSize));
    } else {
        proofSize = core::identity::local_ticket::kTicketSize;
        identityReady = nowValue > 0
                        && core::identity::local_ticket::issue(
                            steam::local_steam_id(),
                            static_cast<std::uint64_t>(nowValue),
                            std::span<std::byte, core::identity::local_ticket::kTicketSize>(
                                requestStorage.data(),
                                core::identity::local_ticket::kTicketSize));
    }
    bool success = identityReady
                   && wire::encode_request(
                       request, std::span(requestStorage).subspan(proofSize), requestSize);
    if (success) {
        requestSize += proofSize;
        const client::network::HttpRequest httpRequest{
            client::network::HttpMethod::post,
            "/v1/social/realtime",
            "application/octet-stream",
            {},
            std::span<const std::byte>(requestStorage).first(requestSize),
            responseStorage,
        };
        client::network::HttpResponse httpResponse{};
        success = client::hooks::external_server::consume(httpRequest, httpResponse)
                  && httpResponse.statusCode == 200
                  && wire::decode_response(
                      std::span<const std::byte>(responseStorage).first(httpResponse.size),
                      response);
    }
    SecureZeroMemory(requestStorage.data(), requestStorage.size());
    return success;
}

[[nodiscard]] std::int32_t retain_chat(const wire::Event& event) noexcept {
    ChatRecord* destination = nullptr;
    for (ChatRecord& record : g_chat) {
        if (!record.occupied) {
            destination = &record;
            break;
        }
    }
    if (destination == nullptr) {
        destination = &g_chat.front();
        for (ChatRecord& record : g_chat) {
            if (record.id < destination->id) {
                destination = &record;
            }
        }
    }
    std::int32_t id = g_nextChatId++;
    if (id <= 0) {
        g_nextChatId = 2;
        id = 1;
    }
    *destination = {};
    destination->lobby = event.lobbyId;
    destination->sender = event.sourcePlatformId;
    destination->id = id;
    destination->payloadSize = event.payloadSize;
    destination->occupied = true;
    std::copy_n(event.payload.begin(), event.payloadSize, destination->payload.begin());
    return id;
}

[[nodiscard]] bool queue_game_join_requested(std::uint64_t friendPlatformId,
                                             const char* connect,
                                             std::size_t connectSize) noexcept {
    if (friendPlatformId == 0 || connect == nullptr || connectSize == 0
        || connectSize >= GameRichPresenceJoinRequested{}.connect.size()) {
        return false;
    }
    GameRichPresenceJoinRequested callback{};
    callback.friendSteamId = friendPlatformId;
    std::memcpy(callback.connect.data(), connect, connectSize);
    return steam::queue_callback(
        kGameRichPresenceJoinRequestedCallback, 0, &callback, sizeof(callback));
}

void report_owner_gameplay_admission(const wire::Event& event,
                                     const char* result,
                                     const char* reason,
                                     std::size_t registrations) noexcept {
    retain_owner_gameplay_admission_diagnostic(event, result, reason, registrations);
    std::array<char, 288> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=steam stage=owner_gameplay_admission result=%s reason=%s transport=silent_descriptor"
        " registrations=%zu sequence=%llu lobby=0x%016llX source=0x%016llX payload_bytes=%zu",
        result,
        reason,
        registrations,
        static_cast<unsigned long long>(event.sequence),
        static_cast<unsigned long long>(event.lobbyId),
        static_cast<unsigned long long>(event.sourcePlatformId),
        event.payloadSize);
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            std::strcmp(result, "queued") == 0 || std::strcmp(result, "retained") == 0
                    || std::strcmp(result, "dispatch") == 0
                ? core::log::Level::info
                : std::strcmp(result, "deferred") == 0 ? core::log::Level::debug
                                                        : core::log::Level::warn,
            {line.data(),
             (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

void service_owner_gameplay_admission(std::uint64_t nowMilliseconds) noexcept {
    (void)nowMilliseconds;
}

enum class NativeProjectionResult : std::uint8_t {
    applied,
    deferred,
    rejected,
};

[[nodiscard]] bool copy_primary_session_state(std::byte* destination,
                                              std::int32_t& mode,
                                              std::uint32_t& revision) noexcept {
    mode = -1;
    revision = 0;
    __try {
        auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (image == nullptr || destination == nullptr) {
            return false;
        }
        using PrimaryGroupSessionAccessor = std::byte*(__fastcall*)(std::int32_t) noexcept;
        const auto accessor = reinterpret_cast<PrimaryGroupSessionAccessor>(
            image + kPrimaryGroupSessionAccessorRva);
        std::byte* const session = accessor(0);
        if (session == nullptr) {
            return false;
        }
        std::memcpy(&mode, session + kSessionModeOffset, sizeof(mode));
        std::memcpy(&revision,
                    session + kSessionMembershipRevisionOffset,
                    sizeof(revision));
        std::memcpy(destination,
                    session + kSessionMembershipStateOffset,
                    middleware::social::membership_projection::kStateSize);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] bool apply_primary_membership(std::byte* decoded,
                                            std::int32_t& mode,
                                            std::uint32_t& revision) noexcept {
    mode = -1;
    revision = 0;
    __try {
        auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        if (image == nullptr || decoded == nullptr) {
            return false;
        }
        using PrimaryGroupSessionAccessor = std::byte*(__fastcall*)(std::int32_t) noexcept;
        using MembershipApplyWrapper = void(__fastcall*)(std::byte*, std::byte*) noexcept;
        const auto accessor = reinterpret_cast<PrimaryGroupSessionAccessor>(
            image + kPrimaryGroupSessionAccessorRva);
        const auto apply = reinterpret_cast<MembershipApplyWrapper>(
            image + kMembershipApplyWrapperRva);
        std::byte* const session = accessor(0);
        if (session == nullptr) {
            return false;
        }
        apply(session, decoded);
        std::memcpy(&mode, session + kSessionModeOffset, sizeof(mode));
        std::memcpy(&revision,
                    session + kSessionMembershipRevisionOffset,
                    sizeof(revision));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void prepare_membership_projection_upload() noexcept {
    alignas(16) std::array<std::byte,
                           middleware::social::membership_projection::kDecodedSize>
        decoded{};
    std::uint32_t stagedRevision = 0;
    AcquireSRWLockShared(&g_stateLock);
    if (g_pendingMembershipProjectionCapture.active) {
        decoded = g_pendingMembershipProjectionCapture.decoded;
        stagedRevision = g_pendingMembershipProjectionCapture.revision;
    }
    ReleaseSRWLockShared(&g_stateLock);
    if (stagedRevision == 0U) {
        return;
    }

    alignas(16) std::array<std::byte,
                           middleware::social::membership_projection::kStateSize>
        state{};
    std::int32_t mode = -1;
    std::uint32_t nativeRevision = 0;
    const std::uint32_t expectedHash =
        middleware::social::membership_projection::read_at<std::uint32_t>(
            decoded, middleware::social::membership_projection::kHashOffset);
    // after TARGET promotion the joiner owns a peer-established PRIMARY. Requiring host-
    // established stranded every validated two-member snapshot; mode 6 is valid only on apply
    const bool exactNativeState = copy_primary_session_state(
                                      state.data(), mode, nativeRevision)
                                  && mode == kPeerEstablishedMode
                                  && nativeRevision == stagedRevision
                                  && middleware::crypto::lookup3::hash_bytes(
                                         state, kNativeSessionHashInitial)
                                         == expectedHash;
    if (!exactNativeState) {
        return;
    }

    std::array<std::byte, wire::kEventPayloadCapacity> encoded{};
    std::size_t written = 0;
    middleware::social::membership_projection::Metadata metadata{};
    if (!middleware::social::membership_projection::compress(
            decoded, state, encoded, written, &metadata)) {
        return;
    }

    bool retained = false;
    const std::uint64_t localPlatformId = steam::local_steam_id();
    AcquireSRWLockExclusive(&g_stateLock);
    const bool localIsMember = std::any_of(
        g_lobby.members.begin(),
        g_lobby.members.begin() + g_lobby.memberCount,
        [localPlatformId](const wire::Member& member) noexcept {
            return member.platformId == localPlatformId;
        });
    if (g_pendingMembershipProjectionCapture.active
        && g_pendingMembershipProjectionCapture.revision == stagedRevision
        && localPlatformId != 0U && g_lobby.id != 0U
        && g_lobby.ownerPlatformId != 0U
        && g_lobby.ownerPlatformId != localPlatformId
        && g_lobby.memberCount == metadata.memberCount && localIsMember) {
        g_pendingMembershipProjectionUpload = {};
        std::copy_n(encoded.begin(), written,
                    g_pendingMembershipProjectionUpload.payload.begin());
        g_pendingMembershipProjectionUpload.payloadSize = written;
        g_pendingMembershipProjectionUpload.revision = stagedRevision;
        g_pendingMembershipProjectionUpload.active = true;
        g_pendingMembershipProjectionCapture = {};
        retained = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);

    if (retained) {
        std::array<char, 208> line{};
        const int logged = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=membership_projection_capture result=retained revision=%u hash=0x%08X bytes=%zu",
            stagedRevision, expectedHash, written);
        if (logged > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(logged),
                                         line.size() - 1U)});
        }
    }
}

// never apply the joiner snapshot unchanged: its foreign local-address row caused the
// earlier self-removal/black-screen regressions
[[nodiscard]] bool arm_membership_projection_leave_suppression(
    std::uint64_t lobbyId,
    std::uint64_t sequence) noexcept;

[[nodiscard]] NativeProjectionResult apply_rebased_membership_projection(
    std::array<std::byte, middleware::social::membership_projection::kDecodedSize>& decoded,
    std::array<std::byte, middleware::social::membership_projection::kStateSize>& desiredState,
    const middleware::social::membership_projection::Metadata& metadata,
    std::uint64_t expectedOwnerPlatformId,
    std::uint64_t expectedSourcePlatformId,
    std::uint64_t lobbyId,
    std::uint64_t eventSequence,
    std::int32_t& mode,
    std::uint32_t& beforeRevision,
    std::uint32_t& afterRevision,
    std::uint32_t& rebasedHash,
    std::uint32_t& appliedHash,
    std::size_t& firstMismatchOffset,
    const char*& failureStage,
    bool& leaveSuppressionArmed,
    middleware::social::membership_projection::OwnerRebaseSummary& rebaseSummary) noexcept {
    namespace projection = middleware::social::membership_projection;
    rebaseSummary = {};
    leaveSuppressionArmed = false;
    appliedHash = 0U;
    firstMismatchOffset = projection::kStateSize;
    failureStage = "none";
    alignas(16) std::array<std::byte,
                           projection::kStateSize>
        currentState{};
    if (!copy_primary_session_state(currentState.data(), mode, beforeRevision)) {
        failureStage = "copy_current";
        return NativeProjectionResult::deferred;
    }
    if (mode != kHostEstablishedMode) {
        failureStage = "wait_host_mode";
        return NativeProjectionResult::deferred;
    }
    const std::uint32_t transportedHash = middleware::crypto::lookup3::hash_bytes(
        desiredState, kNativeSessionHashInitial);
    if (transportedHash != metadata.expectedHash) {
        failureStage = "transport_hash";
        return NativeProjectionResult::rejected;
    }
    if (beforeRevision == (std::numeric_limits<std::uint32_t>::max)()
        || projection::read_at<std::uint32_t>(currentState,
                                              projection::kStateRevisionOffset)
               != beforeRevision) {
        failureStage = "owner_revision";
        return NativeProjectionResult::rejected;
    }
    const char* transformFailure = "incremental_transform";
    if (!projection::rebase_solo_owner(decoded,
                                        desiredState,
                                        currentState,
                                        expectedOwnerPlatformId,
                                        &rebaseSummary,
                                        expectedSourcePlatformId,
                                        &transformFailure)) {
        failureStage = transformFailure;
        return NativeProjectionResult::rejected;
    }

    const std::uint32_t nextRevision = beforeRevision + 1U;
    if (!projection::write_at(decoded, 0U, rebaseSummary.ownerMachineId)
        || !projection::write_at(decoded, projection::kRevisionOffset, nextRevision)
        || !projection::write_at(desiredState,
                                 projection::kStateRevisionOffset,
                                 nextRevision)) {
        failureStage = "revision_write";
        return NativeProjectionResult::rejected;
    }
    rebasedHash = middleware::crypto::lookup3::hash_bytes(
        desiredState, kNativeSessionHashInitial);
    if (rebaseSummary.ownerMachineId == 0U || rebasedHash == 0U
        || !projection::write_at(decoded, projection::kHashOffset, rebasedHash)) {
        failureStage = "hash_write";
        return NativeProjectionResult::rejected;
    }

    // arm after the pure byte/identity checks pass but before native apply can schedule the
    // cleanup
    leaveSuppressionArmed =
        arm_membership_projection_leave_suppression(lobbyId, eventSequence);
    if (!leaveSuppressionArmed) {
        failureStage = "leave_guard";
        return NativeProjectionResult::rejected;
    }

    std::int32_t afterMode = -1;
    if (!apply_primary_membership(decoded.data(), afterMode, afterRevision)) {
        failureStage = "native_apply_call";
        return NativeProjectionResult::rejected;
    }
    alignas(16) std::array<std::byte,
                           projection::kStateSize>
        appliedState{};
    std::uint32_t confirmedRevision = 0;
    if (!copy_primary_session_state(appliedState.data(), afterMode, confirmedRevision)) {
        failureStage = "copy_applied";
        return NativeProjectionResult::rejected;
    }
    afterRevision = confirmedRevision;
    appliedHash = middleware::crypto::lookup3::hash_bytes(
        appliedState, kNativeSessionHashInitial);
    const std::size_t ownerPlayerBase =
        projection::kStatePlayerArrayOffset
        + static_cast<std::size_t>(rebaseSummary.ownerPlayerSlot)
              * projection::kStatePlayerStride;
    if (afterMode != kHostEstablishedMode) {
        failureStage = "post_mode";
        return NativeProjectionResult::rejected;
    }
    if (afterRevision != nextRevision) {
        failureStage = "post_revision";
        return NativeProjectionResult::rejected;
    }
    if (appliedHash != rebasedHash) {
        failureStage = "post_hash";
    }
    for (std::size_t offset = 0U; offset < projection::kStateSize; ++offset) {
        if (appliedState[offset] != desiredState[offset]) {
            firstMismatchOffset = offset;
            break;
        }
    }
    if (firstMismatchOffset != projection::kStateSize) {
        failureStage = "post_state";
        return NativeProjectionResult::rejected;
    }
    if (std::memcmp(appliedState.data() + projection::kStateMemberArrayOffset,
                    currentState.data() + projection::kStateMemberArrayOffset,
                    projection::kStateMemberStride)
            != 0) {
        failureStage = "owner_member_changed";
        return NativeProjectionResult::rejected;
    }
    if (std::memcmp(appliedState.data() + ownerPlayerBase,
                    currentState.data() + ownerPlayerBase,
                    projection::kStatePlayerStride)
        != 0) {
        failureStage = "owner_player_changed";
        return NativeProjectionResult::rejected;
    }
    failureStage = "applied";
    return NativeProjectionResult::applied;
}

[[nodiscard]] bool arm_membership_projection_leave_suppression(
    std::uint64_t lobbyId,
    std::uint64_t sequence) noexcept {
    if (lobbyId == 0U || sequence == 0U) {
        return false;
    }
    bool armed = false;
    AcquireSRWLockExclusive(&g_stateLock);
    const std::uint64_t localPlatformId = steam::local_steam_id();
    if (localPlatformId != 0U && g_lobby.id == lobbyId
        && g_lobby.ownerPlatformId == localPlatformId) {
        g_membershipProjectionOwnerTransition.lobbyId = lobbyId;
        g_membershipProjectionOwnerTransition.sequence = sequence;
        g_membershipProjectionOwnerTransition.suppressLeaveUntil =
            GetTickCount64() + kMembershipProjectionLeaveSuppressionMilliseconds;
        g_membershipProjectionOwnerTransition.suppressLeavePending = true;
        armed = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return armed;
}

void service_host_membership_projection(std::uint64_t nowMilliseconds) noexcept {
    wire::Event event{};
    wire::Lobby lobby{};
    AcquireSRWLockExclusive(&g_stateLock);
    if (!g_pendingHostMembershipProjection.active
        || nowMilliseconds < g_pendingHostMembershipProjection.nextAttempt) {
        ReleaseSRWLockExclusive(&g_stateLock);
        return;
    }
    event = g_pendingHostMembershipProjection.event;
    lobby = g_lobby;
    g_pendingHostMembershipProjection.nextAttempt =
        nowMilliseconds + kMembershipProjectionRetryMilliseconds;
    ReleaseSRWLockExclusive(&g_stateLock);

    const std::uint64_t localPlatformId = steam::local_steam_id();
    const bool sourcePresent = std::any_of(
        lobby.members.begin(),
        lobby.members.begin() + lobby.memberCount,
        [&event](const wire::Member& member) noexcept {
            return member.platformId == event.sourcePlatformId;
        });
    const bool authorized = localPlatformId != 0U && lobby.id == event.lobbyId
                            && lobby.ownerPlatformId == localPlatformId
                            && event.memberPlatformId == localPlatformId
                            && event.sourcePlatformId != localPlatformId && sourcePresent;

    alignas(16) std::array<std::byte,
                           middleware::social::membership_projection::kDecodedSize>
        decoded{};
    alignas(16) std::array<std::byte,
                           middleware::social::membership_projection::kStateSize>
        desiredState{};
    middleware::social::membership_projection::Metadata metadata{};
    const bool decodedOk = authorized
                           && middleware::social::membership_projection::decompress(
                               std::span(event.payload).first(event.payloadSize),
                               decoded,
                               desiredState,
                               &metadata)
                           && metadata.memberCount == lobby.memberCount;
    std::int32_t mode = -1;
    std::uint32_t beforeRevision = 0;
    std::uint32_t afterRevision = 0;
    std::uint32_t rebasedHash = 0;
    std::uint32_t appliedHash = 0;
    std::size_t firstMismatchOffset = middleware::social::membership_projection::kStateSize;
    const char* failureStage = decodedOk ? "not_applied" : "decode_or_authority";
    bool leaveSuppressionArmed = false;
    middleware::social::membership_projection::OwnerRebaseSummary rebaseSummary{};
    const NativeProjectionResult result =
        decodedOk ? apply_rebased_membership_projection(decoded,
                                                         desiredState,
                                                         metadata,
                                                         localPlatformId,
                                                         event.sourcePlatformId,
                                                         event.lobbyId,
                                                         event.sequence,
                                                         mode,
                                                         beforeRevision,
                                                         afterRevision,
                                                         rebasedHash,
                                                         appliedHash,
                                                         firstMismatchOffset,
                                                         failureStage,
                                                         leaveSuppressionArmed,
                                                         rebaseSummary)
                  : NativeProjectionResult::rejected;
    if (result != NativeProjectionResult::deferred) {
        AcquireSRWLockExclusive(&g_stateLock);
        if (g_pendingHostMembershipProjection.active
            && g_pendingHostMembershipProjection.event.sequence == event.sequence) {
            g_pendingHostMembershipProjection = {};
        }
        g_lastHostMembershipProjectionSequence =
            (std::max)(g_lastHostMembershipProjectionSequence, event.sequence);
        ReleaseSRWLockExclusive(&g_stateLock);
    }

    const char* resultName = result == NativeProjectionResult::applied ? "applied"
                             : result == NativeProjectionResult::deferred ? "deferred"
                                                                          : "rejected";
    if (result != NativeProjectionResult::deferred || mode != -1) {
        std::array<char, 896> line{};
        const int written = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=membership_projection_apply result=%s failure=%s authorized=%u decoded=%u leave_guard=%s lobby=0x%016llX source=0x%016llX sequence=%llu mode=%d revision=%u->%u incoming=%u rebased_hash=0x%08X applied_hash=0x%08X mismatch=0x%zX owner_machine=0x%016llX owner_join=0x%016llX owner_player=0x%016llX owner_slot=%u owner_add=%u remote_machine=0x%016llX remote_join=0x%016llX remote_player=0x%016llX remote_platform=0x%016llX remote_add=%u global_add=%u->%u cleared_remote_connections=%u shape=incremental_owner_delta",
            resultName,
            failureStage,
            authorized ? 1U : 0U,
            decodedOk ? 1U : 0U,
            leaveSuppressionArmed ? "retained_async" : "not_armed",
            static_cast<unsigned long long>(event.lobbyId),
            static_cast<unsigned long long>(event.sourcePlatformId),
            static_cast<unsigned long long>(event.sequence),
            mode,
            beforeRevision,
            afterRevision,
            metadata.revision,
            rebasedHash,
            appliedHash,
            firstMismatchOffset,
            static_cast<unsigned long long>(rebaseSummary.ownerMachineId),
            static_cast<unsigned long long>(rebaseSummary.ownerJoinId),
            static_cast<unsigned long long>(rebaseSummary.ownerPlayerId),
            rebaseSummary.ownerPlayerSlot,
            rebaseSummary.ownerPlayerSequence,
            static_cast<unsigned long long>(rebaseSummary.remoteMachineId),
            static_cast<unsigned long long>(rebaseSummary.remoteJoinId),
            static_cast<unsigned long long>(rebaseSummary.remotePlayerId),
            static_cast<unsigned long long>(rebaseSummary.remotePlatformId),
            rebaseSummary.remotePlayerSequence,
            rebaseSummary.priorPlayerAdditionNumber,
            rebaseSummary.nextPlayerAdditionNumber,
            rebaseSummary.clearedRemoteConnections);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             result == NativeProjectionResult::applied
                                 ? core::log::Level::info
                                 : core::log::Level::warn,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
    }
}

void queue_event(const wire::Event& event, std::uint64_t freshLobbyId,
                 std::uint64_t freshSelectedId) noexcept {
    std::uint32_t memberChange = 0U;
    AcquireSRWLockExclusive(&g_stateLock);
    if (event.kind == wire::EventKind::memberChanged) {
        memberChange = lobby_cache::member_change(event, g_lobbies.snapshot(event.lobbyId));
    }
    if (event.kind == wire::EventKind::lobbyDestroyed
        || (event.kind == wire::EventKind::memberChanged
            && event.memberPlatformId == steam::local_steam_id()
            && (memberChange == 2U || memberChange == 4U)
            && !((event.lobbyId == freshLobbyId || event.lobbyId == freshSelectedId)
                 && lobby_cache::contains(g_lobbies.snapshot(event.lobbyId),
                                          event.memberPlatformId)))) {
        g_lobbies.forget(event.lobbyId);
    } else if ((event.kind == wire::EventKind::lobbyChanged
                || event.kind == wire::EventKind::memberChanged)
               && event.lobbyId != freshLobbyId && event.lobbyId != freshSelectedId) {
        g_lobbies.invalidate(event.lobbyId);
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    switch (event.kind) {
    case wire::EventKind::lobbyInvite: {
        PendingInvite pending{};
        pending.senderPlatformId = event.sourcePlatformId;
        pending.lobbyId = event.lobbyId;
        pending.active = event.sourcePlatformId != 0 && event.lobbyId != 0;
        const std::size_t copied = (std::min)(event.payloadSize,
                                              pending.connect.size() - 1U);
        if (copied != 0) {
            std::memcpy(pending.connect.data(), event.payload.data(), copied);
        }
        AcquireSRWLockExclusive(&g_stateLock);
        g_pendingInvite = pending;
        ReleaseSRWLockExclusive(&g_stateLock);
        if (pending.active) {
            core::ui::notice::raise_information(
                "Press Insert, open Account, then accept or decline.");
        }
        std::array<char, 224> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=steam stage=friend_invite action=receive result=%s pending=true"
            " lobby=0x%016llX sender=0x%016llX payload_bytes=%zu",
            pending.active ? "retained" : "invalid",
            static_cast<unsigned long long>(event.lobbyId),
            static_cast<unsigned long long>(event.sourcePlatformId),
            copied);
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                pending.active ? core::log::Level::info : core::log::Level::warn,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
        break;
    }
    case wire::EventKind::lobbyChanged: {
        const LobbyDataUpdate callback{event.lobbyId, event.memberPlatformId, 1, {}};
        (void)steam::queue_callback(kLobbyDataUpdateCallback, 0, &callback, sizeof(callback));
        break;
    }
    case wire::EventKind::memberChanged: {
        const LobbyChatUpdate callback{event.lobbyId,
                                       event.memberPlatformId,
                                       event.sourcePlatformId,
                                       memberChange,
                                       0};
        (void)steam::queue_callback(kLobbyChatUpdateCallback, 0, &callback, sizeof(callback));
        break;
    }
    case wire::EventKind::chatMessage: {
        AcquireSRWLockExclusive(&g_stateLock);
        const std::int32_t messageId = retain_chat(event);
        ReleaseSRWLockExclusive(&g_stateLock);
        const LobbyChatMessage callback{
            event.lobbyId, event.sourcePlatformId, 1, {}, messageId};
        (void)steam::queue_callback(kLobbyChatMessageCallback, 0, &callback, sizeof(callback));
        break;
    }
    case wire::EventKind::lobbyDestroyed: {
        const LobbyDataUpdate callback{event.lobbyId, event.lobbyId, 0, {}};
        (void)steam::queue_callback(kLobbyDataUpdateCallback, 0, &callback, sizeof(callback));
        break;
    }
    case wire::EventKind::rendezvousBatch: {
        middleware::social::rendezvous_batch::View packets{};
        if(event.payloadSize<=event.payload.size()
            && middleware::social::rendezvous_batch::decode(
                std::span(event.payload).first(event.payloadSize),packets)) {
            for(std::size_t i=0;i<packets.count;++i) {
                const auto packet=packets.packets[i];std::uint32_t connection{};
                std::memcpy(&connection,packet.data(),sizeof(connection));
                (void)steam::queue_serialized_rendezvous(event.sourcePlatformId,connection,
                    packet.data()+sizeof(connection),packet.size()-sizeof(connection));
            }
        }
        break;
    }
    case wire::EventKind::rendezvous:
        if (event.payloadSize >= sizeof(std::uint32_t)) {
            std::uint32_t sourceConnectionId{};
            std::memcpy(&sourceConnectionId, event.payload.data(), sizeof(sourceConnectionId));
            (void)steam::queue_serialized_rendezvous(
                event.sourcePlatformId,
                sourceConnectionId,
                event.payload.data() + sizeof(sourceConnectionId),
                event.payloadSize - sizeof(sourceConnectionId));
        }
        break;
    case wire::EventKind::ownerGameplayAdmission: {
        const std::uint64_t localPlatformId = steam::local_steam_id();
        owner_gameplay_admission::Decision decision{};
        AcquireSRWLockExclusive(&g_stateLock);
        decision = owner_gameplay_admission::validate(
            g_lobby, localPlatformId, event, g_lastOwnerGameplayAdmissionSequence);
        if (decision.accepted()
            && (!g_silentOwnerGameplayAdmission.active
                || event.sequence >= g_silentOwnerGameplayAdmission.sequence)) {
            g_silentOwnerGameplayAdmission = {};
            g_silentOwnerGameplayAdmission.lobbyId = event.lobbyId;
            g_silentOwnerGameplayAdmission.sequence = event.sequence;
            g_silentOwnerGameplayAdmission.sourcePlatformId = event.sourcePlatformId;
            std::copy_n(event.payload.begin(),
                        g_silentOwnerGameplayAdmission.descriptor.size(),
                        g_silentOwnerGameplayAdmission.descriptor.begin());
            g_silentOwnerGameplayAdmission.active = true;
        }
        ReleaseSRWLockExclusive(&g_stateLock);
        report_owner_gameplay_admission(
            event,
            decision.accepted() ? "retained" : "rejected",
            owner_gameplay_admission::rejection_name(decision.rejection),
            0);
        break;
    }
    case wire::EventKind::gameplayBootstrapDescriptor: {
        const std::uint64_t localPlatformId = steam::local_steam_id();
        AcquireSRWLockExclusive(&g_stateLock);
        const central_gameplay_bootstrap::Decision decision =
            central_gameplay_bootstrap::validate(g_lobby, localPlatformId, event);
        if (decision.accepted()
            && (!g_centralGameplayBootstrap.active
                || event.sequence >= g_centralGameplayBootstrap.sequence)) {
            g_centralGameplayBootstrap = {};
            g_centralGameplayBootstrap.lobbyId = event.lobbyId;
            g_centralGameplayBootstrap.sequence = event.sequence;
            std::copy_n(event.payload.begin(),
                        g_centralGameplayBootstrap.descriptor.size(),
                        g_centralGameplayBootstrap.descriptor.begin());
            g_centralGameplayBootstrap.active = true;
        }
        ReleaseSRWLockExclusive(&g_stateLock);

        std::array<char, 224> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=steam stage=central_gameplay_bootstrap result=%s reason=%s lobby=0x%016llX sequence=%llu bytes=%zu",
            decision.accepted() ? "retained" : "rejected",
            central_gameplay_bootstrap::rejection_name(decision.rejection),
            static_cast<unsigned long long>(event.lobbyId),
            static_cast<unsigned long long>(event.sequence),
            event.payloadSize);
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                decision.accepted() ? core::log::Level::info : core::log::Level::warn,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
        break;
    }
    case wire::EventKind::membershipProjection: {
        middleware::social::membership_projection::Metadata metadata{};
        const bool validPayload = event.payloadSize != 0U
                                  && middleware::social::membership_projection::inspect(
                                      std::span(event.payload).first(event.payloadSize),
                                      metadata);
        const std::uint64_t localPlatformId = steam::local_steam_id();
        bool retained = false;
        AcquireSRWLockExclusive(&g_stateLock);
        const bool validAuthority = validPayload && localPlatformId != 0U
                                    && g_lobby.id == event.lobbyId
                                    && g_lobby.ownerPlatformId == localPlatformId
                                    && event.memberPlatformId == localPlatformId
                                    && event.sourcePlatformId != 0U
                                    && event.sourcePlatformId != localPlatformId
                                    && std::any_of(
                                        g_lobby.members.begin(),
                                        g_lobby.members.begin() + g_lobby.memberCount,
                                        [&event](const wire::Member& member) noexcept {
                                            return member.platformId
                                                   == event.sourcePlatformId;
                                        });
        if (validAuthority && event.sequence > g_lastHostMembershipProjectionSequence
            && (!g_pendingHostMembershipProjection.active
                || event.sequence >= g_pendingHostMembershipProjection.event.sequence)) {
            g_pendingHostMembershipProjection = {};
            g_pendingHostMembershipProjection.event = event;
            g_pendingHostMembershipProjection.active = true;
            retained = true;
        }
        ReleaseSRWLockExclusive(&g_stateLock);

        std::array<char, 256> line{};
        const int written = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=membership_projection_receive result=%s lobby=0x%016llX source=0x%016llX sequence=%llu revision=%u members=%u bytes=%zu",
            retained ? "retained" : "rejected",
            static_cast<unsigned long long>(event.lobbyId),
            static_cast<unsigned long long>(event.sourcePlatformId),
            static_cast<unsigned long long>(event.sequence),
            metadata.revision,
            static_cast<unsigned>(metadata.memberCount),
            event.payloadSize);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             retained ? core::log::Level::info
                                      : core::log::Level::warn,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
        break;
    }
    }
}

void commit_response(const wire::Response& response, const wire::Lobby* selected) noexcept {
    AcquireSRWLockExclusive(&g_stateLock);
    const std::uint64_t previousLobbyId = g_lobby.id;
    g_cursor = (std::max)(g_cursor, response.cursor);
    lobby_cache::store_response(g_lobbies,response,selected,steam::local_steam_id());
    if (selected != nullptr) {
        g_lobby = *selected;
    }
    if (previousLobbyId != 0U && previousLobbyId != g_lobby.id) {
        g_pendingMembershipProjectionCapture = {};
        g_pendingMembershipProjectionUpload = {};
        g_pendingHostMembershipProjection = {};
    }
    const std::uint64_t localPlatformId = steam::local_steam_id();
    if (localPlatformId == 0U
        || (g_lobby.id != 0U && g_lobby.ownerPlatformId != localPlatformId)
        || (g_pendingNativeFireteamDescriptor.active
            && !g_lobbies.owned(g_pendingNativeFireteamDescriptor.lobbyId, localPlatformId))) {
        g_pendingNativeFireteamDescriptor = {};
    }
    if (localPlatformId == 0U
        || (g_lobby.id != 0U && g_lobby.ownerPlatformId != localPlatformId)
        || (g_publishedNativeFireteamDescriptor.active
            && !g_lobbies.owned(g_publishedNativeFireteamDescriptor.lobbyId, localPlatformId))) {
        g_publishedNativeFireteamDescriptor = {};
    }
    if (g_centralGameplayBootstrap.active
        && g_centralGameplayBootstrap.lobbyId != g_lobby.id) {
        g_centralGameplayBootstrap = {};
    }
    if (g_silentOwnerGameplayAdmission.active
        && g_silentOwnerGameplayAdmission.lobbyId != g_lobby.id) {
        g_silentOwnerGameplayAdmission = {};
    }
    if (g_internalOwnerTransition.lobbyId != 0
        && g_internalOwnerTransition.lobbyId != g_lobby.id) {
        g_internalOwnerTransition = {};
    }
    if (g_membershipProjectionOwnerTransition.lobbyId != 0
        && g_membershipProjectionOwnerTransition.lobbyId != g_lobby.id) {
        g_membershipProjectionOwnerTransition = {};
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    for (std::size_t index = 0; index < response.eventCount; ++index) {
        queue_event(response.events[index], response.lobby.id,
                    selected != nullptr ? selected->id : 0U);
    }
}

DWORD WINAPI poll_worker([[maybe_unused]] void* context) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::poll;
    std::uint32_t projectionRevision = 0U;
    NativeFireteamDescriptorPublication publication{};
    presence_outbox::Update presenceUpdate{};
    const std::uint64_t localPlatformId = steam::local_steam_id();
    AcquireSRWLockShared(&g_stateLock);
    const bool localIsMember = std::any_of(
        g_lobby.members.begin(),
        g_lobby.members.begin() + g_lobby.memberCount,
        [localPlatformId](const wire::Member& member) noexcept {
            return member.platformId == localPlatformId;
        });
    if (g_pendingMembershipProjectionUpload.active && localPlatformId != 0U
        && g_lobby.id != 0U && g_lobby.ownerPlatformId != 0U
        && g_lobby.ownerPlatformId != localPlatformId && g_lobby.memberCount == 2U
        && localIsMember) {
        request.operation = wire::Operation::publishMembershipProjection;
        request.lobbyId = g_lobby.id;
        request.targetPlatformId = g_lobby.ownerPlatformId;
        request.payloadSize = g_pendingMembershipProjectionUpload.payloadSize;
        std::copy_n(g_pendingMembershipProjectionUpload.payload.begin(),
                    request.payloadSize,
                    request.payload.begin());
        projectionRevision = g_pendingMembershipProjectionUpload.revision;
    } else if (g_pendingNativeFireteamDescriptor.active && localPlatformId != 0U
        && g_lobbies.owned(g_pendingNativeFireteamDescriptor.lobbyId, localPlatformId)) {
        publication = g_pendingNativeFireteamDescriptor;
        request.operation = wire::Operation::publishNativeFireteamDescriptor;
        request.lobbyId = publication.lobbyId;
        request.payloadSize = publication.descriptor.size();
        std::copy(publication.descriptor.begin(),
                  publication.descriptor.end(),
                  request.payload.begin());
    } else {
        // presence carries the same response/event stream as a poll; one coalesced value per
        // worker avoids holding the transport through a burst on the graceful-leave tick
        presenceUpdate = g_presenceOutbox.next();
        if (presenceUpdate.revision) {
            request.operation = presenceUpdate.clear ? wire::Operation::clearRichPresence
                                                      : wire::Operation::setRichPresence;
            request.key = presenceUpdate.key;
            request.value = presenceUpdate.value;
        }
    }
    ReleaseSRWLockShared(&g_stateLock);
    wire::Response response{};
    bool success = execute(request, response) && response.result == kResultOk;
    if (presenceUpdate.revision) {
        AcquireSRWLockExclusive(&g_stateLock);
        g_presenceOutbox.complete(presenceUpdate, success);
        ReleaseSRWLockExclusive(&g_stateLock);
    }
    if (success && projectionRevision != 0U) {
        AcquireSRWLockExclusive(&g_stateLock);
        if (g_pendingMembershipProjectionUpload.active
            && g_pendingMembershipProjectionUpload.revision == projectionRevision) {
            g_pendingMembershipProjectionUpload = {};
        }
        ReleaseSRWLockExclusive(&g_stateLock);
        std::array<char, 192> line{};
        const int written = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=membership_projection_publish result=ok lobby=0x%016llX revision=%u bytes=%zu shape=incremental_owner_delta",
            static_cast<unsigned long long>(request.lobbyId),
            projectionRevision,
            request.payloadSize);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
    } else if (success && publication.active) {
        AcquireSRWLockExclusive(&g_stateLock);
        if (g_pendingNativeFireteamDescriptor.active
            && g_pendingNativeFireteamDescriptor.lobbyId == publication.lobbyId
            && g_pendingNativeFireteamDescriptor.descriptor == publication.descriptor) {
            g_publishedNativeFireteamDescriptor = publication;
            g_pendingNativeFireteamDescriptor = {};
        }
        ReleaseSRWLockExclusive(&g_stateLock);
        std::array<char, 192> line{};
        const int written = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=native_fireteam_descriptor_publish result=ok lobby=0x%016llX bytes=%zu source=owner_group_zero",
            static_cast<unsigned long long>(publication.lobbyId),
            publication.descriptor.size());
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
    }
    const wire::Lobby current = lobby_snapshot();
    const initial_lobby_bootstrap::Action bootstrapAction =
        initial_lobby_bootstrap::decide(initial_lobby_bootstrap::Input{
            core::runtime::policy::kDedicatedBackendRequired,
            success,
            steam::local_steam_id(),
            current.id,
        });
    if (bootstrapAction == initial_lobby_bootstrap::Action::ensureSoloLobby) {
        request = {};
        request.operation = wire::Operation::ensureSoloLobby;
        request.valueA = initial_lobby_bootstrap::kLobbyType;
        request.valueB = static_cast<std::int32_t>(wire::kMemberCapacity);
        response = {};
        success = execute(request, response) && response.result == kResultOk
                  && response.lobby.id != 0;
        if (success) {
            const auto memberEnd = response.lobby.members.begin()
                                   + static_cast<std::ptrdiff_t>((std::min)(
                                       response.lobby.memberCount,
                                       response.lobby.members.size()));
            success = std::any_of(response.lobby.members.begin(),
                                  memberEnd,
                                  [localPlatformId](const wire::Member& member) noexcept {
                                      return member.platformId == localPlatformId;
                                  });
        }
        std::array<char, 192> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=steam stage=initial_lobby_bootstrap result=%s lobby=0x%016llX members=%zu",
            success ? "ok" : "fail",
            static_cast<unsigned long long>(response.lobby.id),
            response.lobby.memberCount);
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                success ? core::log::Level::info : core::log::Level::warn,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
    }
    AcquireSRWLockExclusive(&g_stateLock);
    g_lastPollSucceeded = success;
    ReleaseSRWLockExclusive(&g_stateLock);
    // native status queries take the transport mutex, so run this on the existing worker
    // after social locks finish and the game thread never waits
    native_network_diagnostics::service_connection_quality(GetTickCount64());
    return 0;
}

} // namespace

bool stage_rich_presence(const char* key, const char* value) noexcept {
    if (!key) return false;
    const auto keySize = strnlen_s(key, wire::kKeyCapacity);
    const auto valueSize = value ? strnlen_s(value, wire::kValueCapacity) : 0U;
    if (!keySize || keySize >= wire::kKeyCapacity || valueSize >= wire::kValueCapacity)
        return false;
    AcquireSRWLockExclusive(&g_stateLock);
    const bool accepted = g_accepting && g_presenceOutbox.set(
        std::string_view(key, keySize), value ? std::string_view(value, valueSize) : std::string_view{});
    if (accepted && g_presenceOutbox.pending()) g_nextPoll = 0;
    ReleaseSRWLockExclusive(&g_stateLock);
    return accepted;
}

void stage_clear_rich_presence() noexcept {
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_accepting) { g_presenceOutbox.clear(); g_nextPoll = 0; }
    ReleaseSRWLockExclusive(&g_stateLock);
}

bool execute(wire::Request request, wire::Response& response) noexcept {
    std::uint64_t diagnosticRevision = 0;
    AcquireSRWLockExclusive(&g_transportLock);
    AcquireSRWLockShared(&g_stateLock);
    request.cursor = g_cursor;
    if (request.operation == wire::Operation::poll
        && g_ownerAdmissionDiagnosticCount != 0) {
        const auto& diagnostic = g_ownerAdmissionDiagnostics[g_ownerAdmissionDiagnosticHead];
        if (attach_owner_gameplay_admission_diagnostic(diagnostic, request)) {
            diagnosticRevision = diagnostic.revision;
        }
    }
    ReleaseSRWLockShared(&g_stateLock);
    const bool success = transport(request, response);
    if (success) {
        // commit responses in transport order, or the poll worker can overwrite a newer lobby
        // join with an older heartbeat response
        wire::Response selectedResponse{};
        const wire::Lobby* selected = request.operation == wire::Operation::queryLobby
            ? nullptr : &response.lobby;
        if (lobby_cache::returns_operation_lobby(request.operation, response.result)) {
            // Create/Join callbacks need their exact target, group-zero publication needs the separate
            // fireteam. Never repeat a mutation if this query fails; the next poll can recover
            selected = nullptr;
            wire::Request query{};
            query.operation = wire::Operation::queryLobby;
            query.cursor = response.cursor;
            if (transport(query, selectedResponse) && selectedResponse.result == kResultOk) {
                selected = &selectedResponse.lobby;
            }
            // the four-event response can fill with earlier notifications, delaying the old-session
            // LEFT. Only with no cache slot for the target, run at most three read-only queries
            std::array<std::uint64_t,3> retained{};
            AcquireSRWLockExclusive(&g_stateLock);
            lobby_cache::retire_response_departures(g_lobbies,response,selected,steam::local_steam_id());
            if (!g_lobbies.can_remember(response.lobby)) retained=g_lobbies.retained_ids();
            ReleaseSRWLockExclusive(&g_stateLock);
            for (const auto id : retained) {
                if (!id || id==response.lobby.id || (selected && id==selected->id)) continue;
                wire::Request membershipQuery{};wire::Response membershipReply{};
                membershipQuery.operation=wire::Operation::queryLobby;
                membershipQuery.lobbyId=id;membershipQuery.cursor=response.cursor;
                const bool queried=transport(membershipQuery,membershipReply);
                AcquireSRWLockExclusive(&g_stateLock);
                if (queried && lobby_cache::authoritative_absence(membershipQuery,membershipReply))
                    g_lobbies.forget(id);
                const bool available=g_lobbies.can_remember(response.lobby);
                ReleaseSRWLockExclusive(&g_stateLock);
                if (available) break;
            }
        }
        if ((request.operation == wire::Operation::leaveLobby && response.result == kResultOk)
            || (request.operation == wire::Operation::queryLobby && response.result == 15)) {
            AcquireSRWLockExclusive(&g_stateLock);
            g_lobbies.forget(request.lobbyId);
            ReleaseSRWLockExclusive(&g_stateLock);
        }
        if (request.operation == wire::Operation::joinLobby && response.result == kResultOk
            && response.lobby.type != 3) {
            AcquireSRWLockExclusive(&g_stateLock);
            g_lobbies.forget_other_regular(response.lobby.id);
            ReleaseSRWLockExclusive(&g_stateLock);
        }
        commit_response(response, selected);
    }
    ReleaseSRWLockExclusive(&g_transportLock);
    if (success) {
        if (diagnosticRevision != 0) {
            AcquireSRWLockExclusive(&g_stateLock);
            if (g_ownerAdmissionDiagnosticCount != 0
                && g_ownerAdmissionDiagnostics[g_ownerAdmissionDiagnosticHead].revision
                       == diagnosticRevision) {
                g_ownerAdmissionDiagnostics[g_ownerAdmissionDiagnosticHead] = {};
                g_ownerAdmissionDiagnosticHead =
                    (g_ownerAdmissionDiagnosticHead + 1U)
                    % g_ownerAdmissionDiagnostics.size();
                --g_ownerAdmissionDiagnosticCount;
            }
            ReleaseSRWLockExclusive(&g_stateLock);
        }
    }
    return success;
}

wire::Lobby lobby_snapshot() noexcept {
    AcquireSRWLockShared(&g_stateLock);
    const wire::Lobby lobby = g_lobby;
    ReleaseSRWLockShared(&g_stateLock);
    return lobby;
}

wire::Lobby lobby_snapshot(std::uint64_t lobbyId, bool refreshIfNeeded) noexcept {
    AcquireSRWLockShared(&g_stateLock);
    const bool refresh = refreshIfNeeded && g_lobbies.needs_refresh(lobbyId);
    ReleaseSRWLockShared(&g_stateLock);
    if (refresh) {
        wire::Request request{};
        request.operation = wire::Operation::queryLobby;
        request.lobbyId = lobbyId;
        wire::Response response{};
        (void)execute(request, response);
    }
    AcquireSRWLockShared(&g_stateLock);
    const wire::Lobby lobby = g_lobbies.snapshot(lobbyId);
    ReleaseSRWLockShared(&g_stateLock);
    return lobby;
}

bool stage_native_fireteam_descriptor(
    std::uint64_t lobbyId,
    std::span<const std::byte> descriptor) noexcept {
    constexpr std::size_t kMachineOffset = 0U;
    constexpr std::size_t kMethodOffset = 93U;
    constexpr std::size_t kSessionOffset = 110U;
    if (lobbyId == 0U
        || descriptor.size() != middleware::gameplay::descriptor::kDescriptorSize) {
        return false;
    }
    std::uint64_t machineId = 0;
    std::uint64_t onlineSessionId = 0;
    std::memcpy(&machineId, descriptor.data() + kMachineOffset, sizeof(machineId));
    std::memcpy(&onlineSessionId, descriptor.data() + kSessionOffset,
                sizeof(onlineSessionId));
    if (machineId == 0U || onlineSessionId != lobbyId
        || std::to_integer<std::uint8_t>(descriptor[kMethodOffset]) > 7U) {
        return false;
    }

    bool staged = false;
    AcquireSRWLockExclusive(&g_stateLock);
    const std::uint64_t localPlatformId = steam::local_steam_id();
    if (localPlatformId != 0U && g_lobbies.owned(lobbyId, localPlatformId)
        && (g_lobby.id == 0U || g_lobby.ownerPlatformId == localPlatformId)) {
        std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize> exact{};
        std::copy(descriptor.begin(), descriptor.end(), exact.begin());
        const bool alreadyPending = g_pendingNativeFireteamDescriptor.active
                                    && g_pendingNativeFireteamDescriptor.lobbyId == lobbyId
                                    && g_pendingNativeFireteamDescriptor.descriptor == exact;
        const bool alreadyPublished = g_publishedNativeFireteamDescriptor.active
                                      && g_publishedNativeFireteamDescriptor.lobbyId == lobbyId
                                      && g_publishedNativeFireteamDescriptor.descriptor == exact;
        if (!alreadyPending && !alreadyPublished) {
            g_pendingNativeFireteamDescriptor = {};
            g_pendingNativeFireteamDescriptor.lobbyId = lobbyId;
            g_pendingNativeFireteamDescriptor.descriptor = exact;
            g_pendingNativeFireteamDescriptor.active = true;
        }
        staged = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return staged;
}

CentralGameplayBootstrapSnapshot central_gameplay_bootstrap_snapshot() noexcept {
    AcquireSRWLockShared(&g_stateLock);
    const CentralGameplayBootstrapSnapshot snapshot = g_centralGameplayBootstrap;
    ReleaseSRWLockShared(&g_stateLock);
    return snapshot;
}

bool begin_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                            std::uint64_t sequence) noexcept {
    if (lobbyId == 0 || sequence == 0) {
        return false;
    }
    bool prepared = false;
    AcquireSRWLockExclusive(&g_stateLock);
    const std::uint64_t localPlatformId = steam::local_steam_id();
    const bool exactSoloOwner =
        localPlatformId != 0 && g_lobby.id == lobbyId
        && g_lobby.ownerPlatformId == localPlatformId && g_lobby.memberCount == 1
        && g_lobby.members[0].platformId == localPlatformId;
    if (g_centralGameplayBootstrap.active
        && g_centralGameplayBootstrap.lobbyId == lobbyId
        && g_centralGameplayBootstrap.sequence == sequence && exactSoloOwner) {
        g_internalOwnerTransition.lobbyId = lobbyId;
        g_internalOwnerTransition.sequence = sequence;
        // native ingestion may synchronously clean up the local solo lobby, so arm first
        g_internalOwnerTransition.suppressLeaveUntil = GetTickCount64() + 30000U;
        g_internalOwnerTransition.suppressLeavePending = true;
        prepared = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return prepared;
}

bool complete_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                               std::uint64_t sequence) noexcept {
    if (lobbyId == 0 || sequence == 0) {
        return false;
    }
    bool completed = false;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_centralGameplayBootstrap.active
        && g_centralGameplayBootstrap.lobbyId == lobbyId
        && g_centralGameplayBootstrap.sequence == sequence
        && g_lobby.id == lobbyId && g_lobby.ownerPlatformId == steam::local_steam_id()) {
        g_centralGameplayBootstrap = {};
        completed = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return completed;
}

void cancel_central_owner_gameplay_bootstrap(std::uint64_t lobbyId,
                                             std::uint64_t sequence) noexcept {
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_internalOwnerTransition.lobbyId == lobbyId
        && g_internalOwnerTransition.sequence == sequence) {
        g_internalOwnerTransition = {};
    }
    ReleaseSRWLockExclusive(&g_stateLock);
}

SilentOwnerGameplayAdmissionSnapshot silent_owner_gameplay_admission_snapshot() noexcept {
    AcquireSRWLockShared(&g_stateLock);
    const SilentOwnerGameplayAdmissionSnapshot snapshot = g_silentOwnerGameplayAdmission;
    ReleaseSRWLockShared(&g_stateLock);
    return snapshot;
}

bool begin_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                           std::uint64_t sequence) noexcept {
    if (lobbyId == 0 || sequence == 0) {
        return false;
    }
    bool prepared = false;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_silentOwnerGameplayAdmission.active
        && g_silentOwnerGameplayAdmission.lobbyId == lobbyId
        && g_silentOwnerGameplayAdmission.sequence == sequence
        && g_lobby.id == lobbyId && g_lobby.ownerPlatformId == steam::local_steam_id()) {
        g_internalOwnerTransition.lobbyId = lobbyId;
        g_internalOwnerTransition.sequence = sequence;
        g_internalOwnerTransition.suppressLeaveUntil = GetTickCount64() + 30000U;
        g_internalOwnerTransition.suppressLeavePending = true;
        prepared = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return prepared;
}

bool complete_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                              std::uint64_t sequence) noexcept {
    if (lobbyId == 0 || sequence == 0) {
        return false;
    }
    bool completed = false;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_silentOwnerGameplayAdmission.active
        && g_silentOwnerGameplayAdmission.lobbyId == lobbyId
        && g_silentOwnerGameplayAdmission.sequence == sequence
        && g_lobby.id == lobbyId && g_lobby.ownerPlatformId == steam::local_steam_id()) {
        g_lastOwnerGameplayAdmissionSequence =
            (std::max)(g_lastOwnerGameplayAdmissionSequence, sequence);
        g_silentOwnerGameplayAdmission = {};
        completed = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return completed;
}

void cancel_silent_owner_gameplay_admission(std::uint64_t lobbyId,
                                            std::uint64_t sequence) noexcept {
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_internalOwnerTransition.lobbyId == lobbyId
        && g_internalOwnerTransition.sequence == sequence) {
        g_internalOwnerTransition = {};
    }
    ReleaseSRWLockExclusive(&g_stateLock);
}

bool consume_internal_owner_leave_suppression(std::uint64_t lobbyId) noexcept {
    if (lobbyId == 0) {
        return false;
    }
    bool suppress = false;
    const std::uint64_t now = GetTickCount64();
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_membershipProjectionOwnerTransition.suppressLeavePending
        && g_membershipProjectionOwnerTransition.lobbyId == lobbyId
        && now <= g_membershipProjectionOwnerTransition.suppressLeaveUntil
        && g_lobby.id == lobbyId && g_lobby.ownerPlatformId == steam::local_steam_id()) {
        suppress = true;
        g_membershipProjectionOwnerTransition.suppressLeavePending = false;
    } else if (g_membershipProjectionOwnerTransition.lobbyId != 0
               && now > g_membershipProjectionOwnerTransition.suppressLeaveUntil) {
        g_membershipProjectionOwnerTransition = {};
    }
    if (!suppress && g_internalOwnerTransition.suppressLeavePending
        && g_internalOwnerTransition.lobbyId == lobbyId
        && now <= g_internalOwnerTransition.suppressLeaveUntil
        && g_lobby.id == lobbyId && g_lobby.ownerPlatformId == steam::local_steam_id()) {
        suppress = true;
        g_internalOwnerTransition.suppressLeavePending = false;
    } else if (g_internalOwnerTransition.lobbyId != 0
               && now > g_internalOwnerTransition.suppressLeaveUntil) {
        g_internalOwnerTransition = {};
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    return suppress;
}

bool stage_membership_projection(
    std::span<const std::byte> decodedMembership) noexcept {
    if (decodedMembership.size()
        != middleware::social::membership_projection::kDecodedSize) {
        return false;
    }
    const std::uint32_t revision =
        middleware::social::membership_projection::read_at<std::uint32_t>(
            decodedMembership,
            middleware::social::membership_projection::kRevisionOffset);
    const std::int32_t baseRevision =
        middleware::social::membership_projection::read_at<std::int32_t>(
            decodedMembership,
            middleware::social::membership_projection::kBaseRevisionOffset);
    const std::uint16_t memberCount =
        middleware::social::membership_projection::read_at<std::uint16_t>(
            decodedMembership,
            middleware::social::membership_projection::kMemberCountOffset);
    const std::uint16_t playerCount =
        middleware::social::membership_projection::read_at<std::uint16_t>(
            decodedMembership,
            middleware::social::membership_projection::kPlayerCountOffset);
    const std::uint32_t expectedHash =
        middleware::social::membership_projection::read_at<std::uint32_t>(
            decodedMembership,
            middleware::social::membership_projection::kHashOffset);
    if (revision == 0U || baseRevision != 0 || expectedHash == 0U
        || memberCount < 2U || memberCount > 12U || playerCount < 2U
        || playerCount > memberCount) {
        return false;
    }

    const std::uint64_t localPlatformId = steam::local_steam_id();
    bool staged = false;
    AcquireSRWLockExclusive(&g_stateLock);
    const bool localIsMember = std::any_of(
        g_lobby.members.begin(),
        g_lobby.members.begin() + g_lobby.memberCount,
        [localPlatformId](const wire::Member& member) noexcept {
            return member.platformId == localPlatformId;
        });
    if (localPlatformId != 0U && g_lobby.id != 0U
        && g_lobby.ownerPlatformId != 0U
        && g_lobby.ownerPlatformId != localPlatformId
        && g_lobby.memberCount == memberCount && localIsMember
        && (!g_pendingMembershipProjectionCapture.active
            || revision >= g_pendingMembershipProjectionCapture.revision)) {
        g_pendingMembershipProjectionCapture = {};
        std::copy(decodedMembership.begin(),
                  decodedMembership.end(),
                  g_pendingMembershipProjectionCapture.decoded.begin());
        g_pendingMembershipProjectionCapture.revision = revision;
        g_pendingMembershipProjectionCapture.active = true;
        if (g_pendingMembershipProjectionUpload.active
            && revision > g_pendingMembershipProjectionUpload.revision) {
            g_pendingMembershipProjectionUpload = {};
        }
        staged = true;
    }
    ReleaseSRWLockExclusive(&g_stateLock);

    if (staged) {
        std::array<char, 224> line{};
        const int logged = std::snprintf(
            line.data(), line.size(),
            "ev=steam stage=membership_projection_stage result=retained revision=%u base=%d members=%u players=%u hash=0x%08X",
            revision,
            baseRevision,
            static_cast<unsigned>(memberCount),
            static_cast<unsigned>(playerCount),
            expectedHash);
        if (logged > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(logged),
                                         line.size() - 1U)});
        }
    }
    return staged;
}

PendingInvite pending_invite_snapshot() noexcept {
    AcquireSRWLockShared(&g_stateLock);
    const PendingInvite pending = g_pendingInvite;
    ReleaseSRWLockShared(&g_stateLock);
    return pending;
}

bool accept_invite() noexcept {
    const PendingInvite pending = pending_invite_snapshot();
    if (!pending.active || pending.senderPlatformId == 0) {
        return false;
    }
    // join the inviter's lobby before handing the invite to the game: private-match binding
    // admits only participants found in the owner's lobby, and callback 337 never creates that
    bool joined = pending.lobbyId == 0;
    wire::Response joinResponse{};
    if (pending.lobbyId != 0) {
        wire::Request join{};
        join.operation = wire::Operation::joinLobby;
        join.lobbyId = pending.lobbyId;
        joined = execute(join, joinResponse) && joinResponse.result == kResultOk;
    }
    std::array<char, 160> joinLine{};
    const int joinWritten = std::snprintf(
        joinLine.data(),
        joinLine.size(),
        "ev=steam stage=friend_invite action=join lobby=0x%016llX result=%s code=%d",
        static_cast<unsigned long long>(pending.lobbyId),
        joined ? "ok" : "fail",
        joinResponse.result);
    if (joinWritten > 0) {
        core::log::write(core::log::Channel::client,
                         joined ? core::log::Level::info : core::log::Level::warn,
                         {joinLine.data(),
                          (std::min)(static_cast<std::size_t>(joinWritten),
                                     joinLine.size() - 1U)});
    }
    if (!joined) {
        // keep the invite pending so Accept can be retried once the owner is gameplay-ready
        return false;
    }
    const auto connectEnd = std::find(pending.connect.begin(), pending.connect.end(), '\0');
    const bool queued = queue_game_join_requested(
        pending.senderPlatformId,
        pending.connect.data(),
        static_cast<std::size_t>(connectEnd - pending.connect.begin()));
    if (queued) {
        AcquireSRWLockExclusive(&g_stateLock);
        if (g_pendingInvite.active
            && g_pendingInvite.senderPlatformId == pending.senderPlatformId
            && g_pendingInvite.lobbyId == pending.lobbyId) {
            g_pendingInvite = {};
        }
        ReleaseSRWLockExclusive(&g_stateLock);
    }
    core::log::write(core::log::Channel::client,
                     queued ? core::log::Level::info : core::log::Level::warn,
                     queued
                         ? "ev=steam stage=friend_invite action=accept callback=337 result=queued"
                         : "ev=steam stage=friend_invite action=accept callback=337 result=queue_failed");
    return queued;
}

void decline_invite() noexcept {
    AcquireSRWLockExclusive(&g_stateLock);
    g_pendingInvite = {};
    ReleaseSRWLockExclusive(&g_stateLock);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=steam stage=friend_invite action=decline result=cleared");
}

void report_owner_gameplay_admission_dispatch(const void* payload,
                                              std::size_t payloadSize,
                                              std::size_t registrations) noexcept {
    wire::Event event{};
    bool matched = false;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_queuedOwnerGameplayAdmission.active && payload != nullptr
        && payloadSize == sizeof(GameRichPresenceJoinRequested)
        && g_queuedOwnerGameplayAdmission.connectSize != 0
        && g_queuedOwnerGameplayAdmission.connectSize
               < GameRichPresenceJoinRequested{}.connect.size()) {
        std::uint64_t friendPlatformId = 0;
        std::memcpy(&friendPlatformId, payload, sizeof(friendPlatformId));
        const auto* const payloadBytes = static_cast<const std::byte*>(payload);
        const auto& queued = g_queuedOwnerGameplayAdmission;
        matched = friendPlatformId == queued.event.sourcePlatformId
                  && queued.event.payloadSize == queued.connectSize
                  && std::memcmp(payloadBytes + sizeof(friendPlatformId),
                                 queued.event.payload.data(),
                                 queued.connectSize)
                         == 0
                  && payloadBytes[sizeof(friendPlatformId) + queued.connectSize]
                         == std::byte{};
        if (matched) {
            event = queued.event;
            g_dispatchedOwnerGameplayAdmission = queued;
            g_queuedOwnerGameplayAdmission = {};
        }
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    if (matched) {
        report_owner_gameplay_admission(event,
                                        "dispatch",
                                        registrations != 0 ? "none"
                                                           : "callback_unregistered",
                                        registrations);
    }
}

void report_owner_gameplay_admission_dispatch_return(const void* payload,
                                                     std::size_t payloadSize,
                                                     std::size_t registrations) noexcept {
    wire::Event event{};
    bool matched = false;
    AcquireSRWLockShared(&g_stateLock);
    if (g_dispatchedOwnerGameplayAdmission.active && payload != nullptr
        && payloadSize == sizeof(GameRichPresenceJoinRequested)
        && g_dispatchedOwnerGameplayAdmission.connectSize != 0) {
        std::uint64_t friendPlatformId = 0;
        std::memcpy(&friendPlatformId, payload, sizeof(friendPlatformId));
        const auto& dispatched = g_dispatchedOwnerGameplayAdmission;
        matched = friendPlatformId == dispatched.event.sourcePlatformId
                  && dispatched.event.payloadSize == dispatched.connectSize
                  && std::memcmp(static_cast<const std::byte*>(payload)
                                     + sizeof(friendPlatformId),
                                 dispatched.event.payload.data(),
                                 dispatched.connectSize)
                         == 0;
        if (matched) {
            event = dispatched.event;
        }
    }
    ReleaseSRWLockShared(&g_stateLock);
    if (matched) {
        report_owner_gameplay_admission(
            event, "callback_return", "none", registrations);
    }
}

void report_owner_gameplay_admission_launch(std::uint64_t targetPlatformId,
                                            bool accepted) noexcept {
    wire::Event event{};
    bool matched = false;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_dispatchedOwnerGameplayAdmission.active && targetPlatformId != 0
        && targetPlatformId
               == g_dispatchedOwnerGameplayAdmission.event.sourcePlatformId) {
        event = g_dispatchedOwnerGameplayAdmission.event;
        matched = true;
        if (accepted) {
            g_dispatchedOwnerGameplayAdmission = {};
        }
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    if (matched) {
        report_owner_gameplay_admission(event,
                                        "native_launch",
                                        accepted ? "accepted" : "rejected",
                                        0);
    }
}

int copy_chat_entry(std::uint64_t lobbyId,
                    int messageId,
                    std::uint64_t* sender,
                    void* data,
                    int dataCapacity,
                    int* entryType) noexcept {
    if (lobbyId == 0 || messageId <= 0 || dataCapacity < 0
        || (data == nullptr && dataCapacity != 0)) {
        return 0;
    }
    int copied = 0;
    AcquireSRWLockShared(&g_stateLock);
    for (const ChatRecord& record : g_chat) {
        if (!record.occupied || record.lobby != lobbyId || record.id != messageId) {
            continue;
        }
        copied = static_cast<int>((std::min)(record.payloadSize,
                                             static_cast<std::size_t>(dataCapacity)));
        if (copied > 0) {
            std::memcpy(data, record.payload.data(), static_cast<std::size_t>(copied));
        }
        if (sender != nullptr) {
            *sender = record.sender;
        }
        if (entryType != nullptr) {
            *entryType = 1;
        }
        break;
    }
    ReleaseSRWLockShared(&g_stateLock);
    return copied;
}

void activate() noexcept {
    client::crucible::activate();
    const bool senderStarted = rendezvous_sender::start();
    core::log::write(core::log::Channel::client,
                     senderStarted ? core::log::Level::info : core::log::Level::error,
                     senderStarted ? "ev=rendezvous_sender stage=start result=ok"
                                   : "ev=rendezvous_sender stage=start result=fail");
    AcquireSRWLockExclusive(&g_stateLock);
    g_accepting = true;
    g_lastPollSucceeded = true;
    g_nextPoll = 0;
    ReleaseSRWLockExclusive(&g_stateLock);
}

void service(std::uint64_t nowMilliseconds) noexcept {
    client::crucible::service(nowMilliseconds);
    // capture only after retail commits the joiner's full snapshot; the owner later turns it
    // into a nonzero-base incremental peer/player update
    prepare_membership_projection_upload();
    HANDLE completed = nullptr;
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_worker != nullptr && WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) {
        completed = g_worker;
        g_worker = nullptr;
        if (!g_lastPollSucceeded) {
            g_nextPoll = nowMilliseconds + kRetryIntervalMilliseconds;
        } else if (g_presenceOutbox.pending()) {
            g_nextPoll = 0;
        }
    }
    if (g_accepting && g_worker == nullptr && nowMilliseconds >= g_nextPoll) {
        g_worker = CreateThread(nullptr, 0, &poll_worker, nullptr, 0, nullptr);
        g_nextPoll = nowMilliseconds
                     + (g_worker != nullptr ? kPollIntervalMilliseconds
                                            : kRetryIntervalMilliseconds);
    }
    ReleaseSRWLockExclusive(&g_stateLock);
    if (completed != nullptr) {
        (void)CloseHandle(completed);
    }
    service_host_membership_projection(nowMilliseconds);
}

void shutdown() noexcept {
    client::crucible::shutdown();
    HANDLE worker = nullptr;
    AcquireSRWLockExclusive(&g_stateLock);
    g_accepting = false;
    worker = g_worker;
    ReleaseSRWLockExclusive(&g_stateLock);
    rendezvous_sender::shutdown();
    if (worker != nullptr) {
        (void)WaitForSingleObject(worker, INFINITE);
    }
    AcquireSRWLockExclusive(&g_stateLock);
    if (g_worker == worker) {
        g_worker = nullptr;
    }
    g_lobby = {};
    g_lobbies = {};
    g_cursor = 0;
    g_chat = {};
    g_pendingInvite = {};
    g_centralGameplayBootstrap = {};
    g_silentOwnerGameplayAdmission = {};
    g_internalOwnerTransition = {};
    g_membershipProjectionOwnerTransition = {};
    g_nextChatId = 1;
    g_nextPoll = 0;
    g_lastPollSucceeded = true;
    g_lastOwnerGameplayAdmissionSequence = 0;
    g_pendingOwnerGameplayAdmission = {};
    g_queuedOwnerGameplayAdmission = {};
    g_dispatchedOwnerGameplayAdmission = {};
    g_ownerAdmissionDiagnostics = {};
    g_ownerAdmissionDiagnosticHead = 0;
    g_ownerAdmissionDiagnosticCount = 0;
    g_nextOwnerAdmissionDiagnosticRevision = 1;
    g_lastOwnerAdmissionDiagnostic = {};
    g_haveLastOwnerAdmissionDiagnostic = false;
    g_pendingMembershipProjectionCapture = {};
    g_pendingMembershipProjectionUpload = {};
    g_pendingHostMembershipProjection = {};
    g_lastHostMembershipProjectionSequence = 0;
    g_pendingNativeFireteamDescriptor = {};
    g_publishedNativeFireteamDescriptor = {};
    g_presenceOutbox = {};
    ReleaseSRWLockExclusive(&g_stateLock);
    if (worker != nullptr) {
        (void)CloseHandle(worker);
    }
}

} // namespace dawn::steam::runtime::social::realtime_client
