#include "group_host.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <span>
#include <string_view>

#include "../../../core/filesystem/path.h"
#include "../../../core/settings/settings.h"
#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/gameplay/group/member_messages.h"
#include "../../../middleware/gameplay/group/parameter_messages.h"
#include "../../../middleware/gameplay/group/parameter_registry.h"
#include "../../../middleware/gameplay/group/session_messages.h"
#include "../../../middleware/gameplay/group/session_state.h"
#include "../../../middleware/gameplay/group/view_message.h"
#include "../../../state/account/account_state.h"
#include "../../../state/build_data/items/item_catalog.h"
#include "../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../state/activity/destination/activity_destination_validation.h"
#include "../../../state/activity/membership/activity_membership_query.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity/shared/runtime.h"
#include "../../../state/activity/shared/public_admission.h"
#include "../../../state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../../../steam/runtime/runtime.h"
#include "../dtls/dtls_host.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_log.h"
#include "../peer/peer_transport.h"
#include "../../bap/runtime.h"
#include "group_host_sessions.h"
#include "activity_endpoint_identity.h"
#include "activity_host_endpoint.h"
#include "public_player_profile.h"
#include "public_membership_binding.h"
#include "view_session_route.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../RestorationServer/src/account_store.h"
#include "../../../../../RestorationServer/src/realtime_store.h"
#include "central_lobby_identity.h"
#endif

namespace dawn::server::gameplay::group {

namespace {

namespace wire = middleware::gameplay::group;
namespace bits = middleware::encoding::bits;
namespace descriptor = middleware::gameplay::descriptor;

// a method-6/7 kind-22 NetAddr is 1083 encoded bits; the old 128-byte staging buffer rejected native descriptors
constexpr std::size_t kBodyCapacity = wire::kHostReestablishSize;
constexpr std::size_t kMembershipBodyCapacity = 1024;
constexpr std::size_t kParameterAuthorityBodyCapacity = kMembershipBodyCapacity;
constexpr std::size_t kReliableInnerHeaderBits = 6 + 18;
constexpr std::size_t kMaximumReliableBodyBits =
    state::gameplay::kReassemblyCapacity * 8 - kReliableInnerHeaderBits;
constexpr std::uint64_t kParameterMaskBits = 0x1FFFFFF;
constexpr std::uint64_t kActivitySelectionAuthorityMask =
    (std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::activitySelection))
    | (std::uint64_t{1}
       << static_cast<std::uint8_t>(wire::Parameter::activitySelectionResponses));
constexpr std::size_t kParameterNameCapacity = 640;
constexpr std::uint32_t kHostMemberIndex = 0;
constexpr std::size_t kAdmittedCapacity = 48;
constexpr std::uint32_t kAllMembers = 0xFFFFFFFF;
constexpr std::uint64_t kRetryInterval = 250;
// reproduces the baseline host-reestablish that lets the authored manager finish mode 5
// all migration delays count from activity-host publication
constexpr std::uint64_t kInitialHostReestablishDelay = 5000;
constexpr std::size_t kNativeIdentity128Offset =
    sizeof(std::uint64_t) + descriptor::kNetAddrSize;
constexpr std::size_t kNativeIdentity144Offset = kNativeIdentity128Offset + 16U;
static_assert(kNativeIdentity144Offset + 18U == descriptor::kDescriptorSize);
constexpr std::uint32_t kPeerPlayerSlot = 0;
constexpr std::uint32_t kFirstPlayerAdditionNumber = 0;
constexpr std::int32_t kOrdinaryPlayerProfileType = 0;
constexpr std::uint8_t kBootReasonFromUi = 1;

struct Admitted {
    std::uint64_t activityMemberKey{};
    std::uint64_t activityAccount{}, activitySource{}, activityCreatedRevision{}, activityJoinedRevision{};
    std::uint64_t activityPlayerId{};
    state::gameplay::Endpoint endpoint{};
    std::uint64_t joinId{};
    std::uint64_t playerId{};
    std::uint32_t playerRequestSequence{};
    std::uint32_t membershipAdditionNumber{};
    std::array<std::byte, 36> playerIdentity{};
    wire::PlayerProfileSupplement playerProfileSupplement{};
    std::uint64_t joiningPlatformId{};
    std::uint64_t lobbyId{};
    std::uint64_t lobbyOwnerPlatformId{};
    std::uint64_t sessionId{};
    bool occupied{};
    bool hasPlayer{};
    bool playerIdentityPresent{};
    bool playerRequestSequencePresent{};
    bool membershipAdditionNumberPresent{};
    bool initialMembershipPublished{};
    bool initialParametersPublished{};
    bool joinComplete{};
    bool joinPublished{};
    bool activityHostPublished{};
    std::uint64_t activityHostPublishedAt{};
    bool initialHostReestablishPublished{};
    std::uint64_t peerReestablishGeneration{};
    std::uint64_t peerReestablishMembershipGeneration{};
    wire::HostReestablish migrationDescriptor{};
    bool migrationIdentityCaptured{};
    std::uint64_t migratedSessionId{};
    bool migratedActivityHostPublished{};
    bool playerPublished{};
    // set once this peer consumes the native remove half of its ID36 -> ID34 identity replacement
    // the source must see that remove so it emits ID34; observers keep their last table until commit
    bool playerReplacementPending{};
    bool membershipDirty{};
    std::array<std::byte, descriptor::kNetAddrSize> publicPeerAddress{};
    bool publicPeerConnectionPresent{};
    wire::EncodedParameterBody activitySelectionProposal{};
    wire::EncodedParameterBody activitySelectionResponsesProposal{};
    std::uint64_t parameterAuthorityDirtyMask{};
    std::uint64_t lastRetry{};
    std::uint64_t lastUse{};
};

struct PlayerAdditionState final {
    std::uint64_t sessionId{};
    std::uint64_t syntheticOwnerPlatformId{};
    std::uint32_t membershipRevision{};
    std::uint32_t nextAdditionNumber{};
    std::uint32_t syntheticOwnerAdditionNumber{};
    bool occupied{};
    bool syntheticOwnerAdditionNumberPresent{};
};

// the peer resolves a session through a two-element array (current and target), so a third is one it left
constexpr std::size_t kPublicSessionCapacity = 2;

std::atomic<std::uint32_t> g_membershipProjectionDumpCount{0};
std::atomic<std::uint64_t> g_admitClock{0};
SRWLOCK g_admittedLock{SRWLOCK_INIT};
std::array<Admitted, kAdmittedCapacity> g_admitted{};
std::array<PlayerAdditionState, kAdmittedCapacity> g_playerAdditions{};

[[nodiscard]] bool write_server_analysis_artifact(std::wstring_view filename,
                                                  std::span<const std::byte> bytes) noexcept {
    const HMODULE executable = GetModuleHandleW(nullptr);
    core::path::Buffer path{};
    if (executable == nullptr || !core::path::artifact_directory(executable, path)
        || !core::path::append(path, L"\\analysis")) {
        return false;
    }
    if (CreateDirectoryW(path.chars.data(), nullptr) == FALSE
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    if (!core::path::append(path, filename)) {
        return false;
    }
    const HANDLE file = CreateFileW(path.chars.data(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool complete = bytes.size() <= MAXDWORD
                          && WriteFile(file,
                                       bytes.data(),
                                       static_cast<DWORD>(bytes.size()),
                                       &written,
                                       nullptr)
                                 != FALSE
                          && written == bytes.size() && FlushFileBuffers(file) != FALSE;
    (void)CloseHandle(file);
    return complete;
}

void dump_membership_projection(const wire::MembershipUpdate& update,
                                std::span<const std::byte> body,
                                std::size_t bodyBits,
                                std::uint32_t hash) noexcept {
    if (update.members.size() < 2U || update.players.size() < 2U) {
        return;
    }
    const std::uint32_t index =
        g_membershipProjectionDumpCount.fetch_add(1U, std::memory_order_relaxed);
    if (index >= 8U) {
        return;
    }
    static thread_local wire::SessionState state{};
    wire::build_session_state(update, state);
    std::array<wchar_t, 176> stateFilename{};
    std::array<wchar_t, 176> wireFilename{};
    const int stateNameLength = std::swprintf(
        stateFilename.data(),
        stateFilename.size(),
        L"\\membership_server_state.%u.rev_%u.hash_%08X.bin",
        index,
        update.revision,
        hash);
    const int wireNameLength = std::swprintf(
        wireFilename.data(),
        wireFilename.size(),
        L"\\membership_server_wire.%u.rev_%u.hash_%08X.bits_%llu.bin",
        index,
        update.revision,
        hash,
        static_cast<unsigned long long>(bodyBits));
    const bool stateWritten = stateNameLength > 0
                              && write_server_analysis_artifact(stateFilename.data(), state);
    const bool wireWritten = wireNameLength > 0
                             && write_server_analysis_artifact(wireFilename.data(), body);
    report(stateWritten && wireWritten ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=membership_projection_capture result=%s index=%u revision=%u hash=0x%08X state_bytes=%llu wire_bytes=%llu wire_bits=%llu",
           stateWritten && wireWritten ? "ok" : "write",
           index,
           update.revision,
           hash,
           static_cast<unsigned long long>(state.size()),
           static_cast<unsigned long long>(body.size()),
           static_cast<unsigned long long>(bodyBits));
}

template <std::size_t Size>
[[nodiscard]] std::uint64_t identity_word(const std::array<std::byte, Size>& bytes,
                                          std::size_t offset,
                                          std::size_t width = sizeof(std::uint64_t)) noexcept {
    if (offset >= Size || width > sizeof(std::uint64_t) || offset + width > Size) {
        return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

constexpr wire::MemberState kJoinMemberState = wire::MemberState::ready;

// three peer checks pin this to exactly ready: the joining entry must be at least joined, not
// established, and the request waits until every member with the join id reads ready
static_assert(static_cast<std::uint8_t>(kJoinMemberState)
                  >= static_cast<std::uint8_t>(wire::MemberState::joined),
              "the published member state must clear the peer's own join bar");
static_assert(kJoinMemberState == wire::MemberState::ready,
              "the request advances only when every member carrying the join id reads ready");

template <std::size_t Capacity, typename Body>
[[nodiscard]] bool send_reliable_sized(const state::gameplay::Endpoint& endpoint,
                                       std::uint64_t sessionId,
                                       std::uint8_t id,
                                       std::uint32_t declaredSize,
                                       Body write) noexcept {
    std::array<std::byte, Capacity> body{};
    bits::Writer writer(body);
    std::size_t size = 0;
    if (!write(writer) || !writer.finish(size)) {
        report(core::log::Level::error,
               "ev=gameplay stage=reliable_encode result=rejected session=0x%016llX id=%u "
               "declared_size=%u capacity=%llu bits_written=%llu",
               static_cast<unsigned long long>(sessionId),
               static_cast<unsigned>(id),
               declaredSize,
               static_cast<unsigned long long>(body.size()),
               static_cast<unsigned long long>(writer.bit_count()));
        return false;
    }
    const bool queued = peer::enqueue_reliable_to(
        endpoint, sessionId, id, declaredSize, {body.data(), size}, writer.bit_count());
    if (!queued) {
        report(core::log::Level::debug,
               "ev=gameplay stage=reliable_enqueue result=deferred session=0x%016llX id=%u "
               "declared_size=%u body_bytes=%llu body_bits=%llu",
               static_cast<unsigned long long>(sessionId),
               static_cast<unsigned>(id),
               declaredSize,
               static_cast<unsigned long long>(size),
               static_cast<unsigned long long>(writer.bit_count()));
    }
    return queued;
}

template <typename Body>
[[nodiscard]] bool send_reliable(const state::gameplay::Endpoint& endpoint,
                                 std::uint64_t sessionId,
                                 std::uint8_t id,
                                 std::uint32_t declaredSize,
                                 Body write) noexcept {
    return send_reliable_sized<kBodyCapacity>(
        endpoint, sessionId, id, declaredSize, write);
}

[[nodiscard]] Admitted* find_admitted(const state::gameplay::Endpoint& peer,
                                      std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return nullptr;
    }
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.sessionId == sessionId && entry.endpoint.address == peer.address
            && entry.endpoint.port == peer.port) {
            return &entry;
        }
    }
    return nullptr;
}

// view bodies name only the activity token so they can't route themselves; the most recently
// admitted endpoint-local session is the target being established
[[nodiscard]] std::uint64_t resolve_view_session(
    const state::gameplay::Endpoint& peer,
    std::uint64_t transportSessionId) noexcept {
    if (transportSessionId != 0) {
        return transportSessionId;
    }

    std::array<view_session_route::Candidate, kAdmittedCapacity> candidates{};
    AcquireSRWLockShared(&g_admittedLock);
    for (std::size_t index = 0; index < g_admitted.size(); ++index) {
        const Admitted& admitted = g_admitted[index];
        candidates[index].sessionId = admitted.sessionId;
        candidates[index].lastUse = admitted.lastUse;
        candidates[index].occupied = admitted.occupied;
        candidates[index].endpointMatches = admitted.endpoint.address == peer.address
                                              && admitted.endpoint.port == peer.port;
    }
    ReleaseSRWLockShared(&g_admittedLock);
    return view_session_route::resolve(transportSessionId, candidates);
}

[[nodiscard]] PlayerAdditionState*
find_player_additions(std::uint64_t sessionId) noexcept {
    for (PlayerAdditionState& state : g_playerAdditions) {
        if (state.occupied && state.sessionId == sessionId) {
            return &state;
        }
    }
    return nullptr;
}

[[nodiscard]] PlayerAdditionState*
claim_player_additions(std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return nullptr;
    }
    if (PlayerAdditionState* const existing = find_player_additions(sessionId);
        existing != nullptr) {
        return existing;
    }
    for (PlayerAdditionState& state : g_playerAdditions) {
        if (!state.occupied) {
            state = {};
            state.occupied = true;
            state.sessionId = sessionId;
            state.nextAdditionNumber = kFirstPlayerAdditionNumber;
            return &state;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t allocate_player_addition(PlayerAdditionState& state) noexcept {
    const std::uint32_t allocated = state.nextAdditionNumber;
    state.nextAdditionNumber = wire::advance_player_addition_number(allocated);
    return allocated;
}

void release_player_additions_if_unused(std::uint64_t sessionId) noexcept {
    const bool stillUsed = std::any_of(
        g_admitted.begin(), g_admitted.end(), [sessionId](const Admitted& entry) noexcept {
            return entry.occupied && entry.sessionId == sessionId;
        });
    if (stillUsed) {
        return;
    }
    if (PlayerAdditionState* const state = find_player_additions(sessionId); state != nullptr) {
        *state = {};
    }
}

void clear_parameter_authority(std::uint64_t sessionId) noexcept {
    for (Admitted& entry : g_admitted) {
        if (!entry.occupied || entry.sessionId != sessionId) {
            continue;
        }
        entry.activitySelectionProposal = {};
        entry.activitySelectionResponsesProposal = {};
        entry.parameterAuthorityDirtyMask = 0;
    }
}

[[nodiscard]] Admitted* claim(const state::gameplay::Endpoint& peer,
                              std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return nullptr;
    }
    if (claim_player_additions(sessionId) == nullptr) {
        return nullptr;
    }
    const std::uint64_t use = g_admitClock.fetch_add(1) + 1;
    if (Admitted* const existing = find_admitted(peer, sessionId); existing != nullptr) {
        existing->lastUse = use;
        return existing;
    }
    for (Admitted& entry : g_admitted) {
        if (!entry.occupied) {
            entry.occupied = true;
            entry.endpoint = peer;
            entry.sessionId = sessionId;
            entry.lastUse = use;
            clear_parameter_authority(sessionId);
            for (Admitted& existing : g_admitted) {
                if (existing.occupied && &existing != &entry && existing.sessionId == sessionId) {
                    existing.membershipDirty = true;
                }
            }
            return &entry;
        }
    }
    return nullptr;
}

void mark_other_members_dirty(std::uint64_t sessionId,
                              const state::gameplay::Endpoint& changed) noexcept {
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.sessionId == sessionId
            && (entry.endpoint.address != changed.address || entry.endpoint.port != changed.port)) {
            entry.membershipDirty = true;
        }
    }
}

// observer publication is deferred while a member is between native ID36 remove and ID34 add,
// so a transient source-only replacement never looks like a disconnect to another client
[[nodiscard]] bool session_player_replacement_pending(std::uint64_t sessionId) noexcept {
    for (const Admitted& entry : g_admitted) {
        if (entry.occupied && entry.sessionId == sessionId && entry.playerReplacementPending) {
            return true;
        }
    }
    return false;
}

#if defined(RESTORATION_DEDICATED_SERVER)
// the native join carries no platform id; the later player-add's Codec-A binding is checked against
// this result, and the build-local id is only a bootstrap preference, never an assertion
[[nodiscard]] bool resolve_central_lobby_member(
    Admitted& record,
    std::uint64_t preferredPlatformId,
    middleware::social::realtime::Lobby& lobby,
    central_lobby_identity::Result& result) noexcept {
    lobby = {};
    result = central_lobby_identity::Result::invalidLobby;

    const endpoint::Identity endpointIdentity = endpoint::identity();
    const std::uint64_t sessionLobbyId = endpoint::recover_lobby_id(
        endpointIdentity.machineId, record.sessionId);
    if (sessionLobbyId == 0
        || !::restoration::realtime_store::lobby_snapshot(sessionLobbyId, lobby)
        || lobby.id != sessionLobbyId) {
        return false;
    }

    for (const Admitted& existing : g_admitted) {
        if (!existing.occupied || &existing == &record || existing.sessionId != record.sessionId
            || existing.lobbyId == 0 || existing.joiningPlatformId == 0) {
            continue;
        }
        if (sessionLobbyId != existing.lobbyId) {
            return false;
        }
        middleware::social::realtime::Lobby candidate{};
        if (!::restoration::realtime_store::lobby_for_member(
                existing.joiningPlatformId, candidate)
            || candidate.id != sessionLobbyId) {
            return false;
        }
    }

    std::array<std::uint64_t, kAdmittedCapacity> boundPlatformIds{};
    std::size_t boundCount = 0;
    for (const Admitted& existing : g_admitted) {
        if (!existing.occupied || &existing == &record || existing.sessionId != record.sessionId
            || existing.lobbyId != lobby.id || existing.joiningPlatformId == 0) {
            continue;
        }
        for (std::size_t index = 0; index < boundCount; ++index) {
            if (boundPlatformIds[index] == existing.joiningPlatformId) {
                return false;
            }
        }
        boundPlatformIds[boundCount++] = existing.joiningPlatformId;
    }

    std::uint64_t resolvedPlatformId = 0;
    result = central_lobby_identity::resolve_joiner_for_session(
        endpointIdentity.machineId,
        record.sessionId,
        lobby,
        std::span<const std::uint64_t>(boundPlatformIds.data(), boundCount),
        preferredPlatformId,
        resolvedPlatformId);
    if (resolvedPlatformId == 0
        || (result != central_lobby_identity::Result::resolvedPreferred
            && result != central_lobby_identity::Result::resolvedOnlyRemaining
            && result != central_lobby_identity::Result::resolvedOnlyNonOwner)) {
        return false;
    }
    record.joiningPlatformId = resolvedPlatformId;
    record.lobbyId = lobby.id;
    record.lobbyOwnerPlatformId = lobby.ownerPlatformId;
    return true;
}

// region group ids derive from region and endpoint identities, not Steam lobby ids, so the lobby
// resolver would yield an empty lobby and withhold the snapshot until the native join times out
[[nodiscard]] bool resolve_activity_member(Admitted& record,
                                           std::uint64_t& sourceActivitySessionId,
                                           std::uint64_t& accountSoid,
                                           const middleware::gameplay::peer::JoinMembers* nativeMembers,
                                           const middleware::gameplay::peer::JoinPlayer*& nativePlayer,
                                           const char*& bindingStage) noexcept {
    nativePlayer=nullptr;
    bindingStage="source_private";
    sourceActivitySessionId = source_activity_session(record.sessionId);
    state::activity::SessionRecord source{};
    std::array<state::activity::SessionRecord,32> publicSources{};
    const auto publicHost=held_host_session(record.sessionId);
    const bool publicWorld=state::activity::shared::public_source_snapshots(publicHost,publicSources);
    if(publicWorld) {
        std::array<std::byte,descriptor::kNetAddrSize> linkAddress{};
        bindingStage="public_native_tables";
        if(!nativeMembers || !peer::remote_address_for(record.endpoint,record.sessionId,linkAddress)) return false;
        const auto* selected=activity_endpoint_identity::select_player(*nativeMembers,linkAddress);
        if(!selected) return false;
        std::array<activity_endpoint_identity::Participant,32> candidates{};
        for(std::size_t i=0;i<publicSources.size();++i) {
            const auto& participant=publicSources[i];
            if(!participant.joined || !participant.membership.hasIdentity) continue;
            std::uint64_t platform{},revision{};
            state::AccountState profile{};
            std::array<char,::restoration::account_store::kAccountNameCapacity> name{};
            if(::restoration::account_store::profile_by_account(participant.ownerAccountSoid,platform,profile,revision,name))
                candidates[i]={platform,state::activity::shared::as_reservation(participant.membership.identity).joinIdentity,
                    participant.ownerAccountSoid,participant.memberKey,true};
        }
        bindingStage="public_player_identity";
        const auto index=activity_endpoint_identity::resolve_participant(candidates,*selected);
        if(index==candidates.size()) return false;
        const auto& identity=candidates[index];
        if(record.joiningPlatformId!=0 && record.joiningPlatformId!=identity.platformId) return false;
        for(const auto& existing:g_admitted)
            if(&existing!=&record && existing.occupied && existing.sessionId==record.sessionId
                && existing.joiningPlatformId==identity.platformId) return false;
        bindingStage="public_source_revalidation";
        if(!state::activity::shared::public_source_snapshot(publicHost,identity.accountId,source)
            || source.sessionId!=publicSources[index].sessionId
            || source.createdRevision!=publicSources[index].createdRevision
            || state::activity::shared::as_reservation(source.membership.identity).joinIdentity!=selected->playerId)
            return false;
        sourceActivitySessionId=source.sessionId;
        nativePlayer=selected;
    } else if (!source_activity_snapshot(record.sessionId,source) || !source.joined
        || !source.membership.hasIdentity) return false;
    // P2P fireteams have no dedicated central endpoint; correlate this endpoint's peer->player table
    // with the separately authenticated PRIVATE participant
    if (!publicWorld && source.sharedLeaseParts>1) {
        std::array<std::byte,descriptor::kNetAddrSize> linkAddress{};
        bindingStage="native_tables";
        if (!nativeMembers) return false;
        bindingStage="link_address";
        if (!peer::remote_address_for(record.endpoint,record.sessionId,linkAddress)) return false;
        bindingStage="peer_player_mapping";
        const auto* selected=activity_endpoint_identity::select_player(*nativeMembers,linkAddress);
        if (!selected) return false;
        std::array<std::uint64_t,32> candidates{};
        bindingStage="private_participant_snapshot";
        if (!state::activity::shared::participant_accounts_snapshot(source,candidates)) return false;
        std::array<activity_endpoint_identity::Participant,32> participants{};
        std::uint64_t ownerPlatform{},ownerRevision{};
        state::AccountState ownerProfile{};
        std::array<char,::restoration::account_store::kAccountNameCapacity> ownerName{};
        middleware::social::realtime::Lobby ownerLobby{};
        bindingStage="owner_lobby";
        if (!::restoration::account_store::profile_by_account(source.ownerAccountSoid,ownerPlatform,
                ownerProfile,ownerRevision,ownerName)
            || !::restoration::realtime_store::lobby_for_member(ownerPlatform,ownerLobby)
            || ownerLobby.id==0) return false;
        for (std::size_t i=0;i<candidates.size();++i) {
            const auto accountId=candidates[i];
            if (accountId==0) continue;
            std::uint64_t platform{},revision{};
            state::AccountState profile{};
            std::array<char,::restoration::account_store::kAccountNameCapacity> name{};
            middleware::social::realtime::Lobby lobby{};
            state::activity::SessionRecord participant{};
            if (::restoration::account_store::profile_by_account(accountId,platform,profile,revision,name)
                && ::restoration::realtime_store::lobby_for_member(platform,lobby) && lobby.id==ownerLobby.id
                && state::activity::shared::participant_snapshot(source,accountId,participant)
                && participant.joined && participant.membership.hasIdentity
                && participant.membership.identity.accountSoid==accountId) {
                const auto playerId=state::activity::shared::as_reservation(participant.membership.identity).joinIdentity;
                participants[i]={platform,playerId,accountId,participant.memberKey,true};
            }
        }
        bindingStage="private_player_identity";
        const auto resolved=activity_endpoint_identity::resolve_participant(participants,*selected);
        if (resolved==participants.size()) return false;
        const auto& identity=participants[resolved];
        bindingStage="endpoint_identity_changed";
        if (record.joiningPlatformId!=0 && record.joiningPlatformId!=identity.platformId) return false;
        bindingStage="duplicate_endpoint_claim";
        for (const auto& existing:g_admitted) {
            if (&existing!=&record && existing.occupied && existing.sessionId==record.sessionId
                && existing.joiningPlatformId==identity.platformId) return false;
        }
        state::activity::SessionRecord participant{};
        bindingStage="participant_revalidation";
        if (!state::activity::shared::participant_snapshot(source,identity.accountId,participant)
            || !participant.joined || !participant.membership.hasIdentity
            || state::activity::shared::as_reservation(participant.membership.identity).joinIdentity!=selected->playerId)
            return false;
        source=participant;
        nativePlayer=selected;
    }
    accountSoid=source.membership.identity.accountSoid;
    std::uint64_t platformId = 0;
    state::AccountState account{};
    std::uint64_t revision = 0;
    std::array<char, ::restoration::account_store::kAccountNameCapacity> displayName{};
    bindingStage="account_profile";
    if (sourceActivitySessionId == state::activity::kAbsentSessionId || accountSoid == 0
        || !::restoration::account_store::profile_by_account(
            accountSoid, platformId, account, revision, displayName)
        || platformId == 0) {
        return false;
    }
    record.joiningPlatformId = platformId;
    record.activityMemberKey=source.memberKey;
    record.activityAccount=accountSoid;
    record.activitySource=source.sessionId;
    record.activityCreatedRevision=source.createdRevision;
    record.activityJoinedRevision=source.joinedRevision;
    record.activityPlayerId=state::activity::shared::as_reservation(source.membership.identity).joinIdentity;
    record.lobbyId = 0;
    record.lobbyOwnerPlatformId = 0;
    bindingStage=nativePlayer?"native_private_match":"solo_source";
    return true;
}
#endif

[[nodiscard]] std::uint64_t member_machine_id(const Admitted& record) noexcept {
    if (record.activityMemberKey!=0) return record.activityMemberKey;
    // a public-region group is the managed-session shell for an ActivityClient, and retail
    // carries the PRIVATE member into it; the Steam id makes three machines, PUBLIC TARGET fails
    const std::uint64_t sourceActivitySessionId = source_activity_session(record.sessionId);
    if (sourceActivitySessionId != state::activity::kAbsentSessionId) {
        state::activity::SessionRecord source{};
        const std::uint64_t activityMemberKey =
            source_activity_snapshot(record.sessionId, source) && source.joined
                ? source.memberKey : 0;
        if (activityMemberKey != 0) {
            return activityMemberKey;
        }
    }

    // the managed-session player id changes during remove/add; machine identity must stay stable or each
    // revision describes a different peer. Central fireteams keep their immutable platform id
    if (record.joiningPlatformId != 0) {
        return record.joiningPlatformId;
    }
    const std::uint64_t endpointWord =
        (static_cast<std::uint64_t>(record.endpoint.address) << 16U) | record.endpoint.port;
    const std::uint64_t value = record.sessionId ^ endpointWord ^ 0x5253544F52415449ULL;
    return value == 0 ? 1 : value;
}

void member_self_address(const Admitted& record,
                         std::array<std::byte, descriptor::kNetAddrSize>& output) noexcept {
    output = {};
    if (!peer::remote_address_for(record.endpoint, record.sessionId, output)) {
        descriptor::write_net_addr(record.endpoint.address, record.endpoint.port, output);
    }
}

// PC 1.59 keys compact-roster selection and ID33 boot-machine on NetAddr; the exact self address is
// only for the recipient's own row, since reusing it for remote rows aliases two members into one entry
void member_address_for_recipient(
    const Admitted& candidate,
    const Admitted& recipient,
    std::array<std::byte, descriptor::kNetAddrSize>& output) noexcept {
    const bool self = candidate.endpoint.address == recipient.endpoint.address
                      && candidate.endpoint.port == recipient.endpoint.port;
    if (self) {
        member_self_address(candidate, output);
        return;
    }
    descriptor::write_net_addr(candidate.endpoint.address, candidate.endpoint.port, output);
}

[[nodiscard]] bool public_peer_address(
    const Admitted& candidate,
    std::array<std::byte, descriptor::kNetAddrSize>& output) noexcept {
    output = {};
    if (!public_membership_binding::ready(candidate) || !candidate.joinComplete
        || source_activity_session(candidate.sessionId) == state::activity::kAbsentSessionId) {
        return false;
    }
    const auto host = held_host_session(candidate.sessionId);
    const state::activity::shared::PublicMemberConnectionFence fence{candidate.activityAccount,
        candidate.activitySource,candidate.activityCreatedRevision,candidate.activityJoinedRevision};
    core::protocol::ActivityPeerConnection connection{};
    if (!state::activity::shared::public_member_connection(host,candidate.activityMemberKey,
            candidate.activityPlayerId,connection,fence)) return false;
    output = connection.peer;
    return true;
}

[[nodiscard]] bool public_peer_address_matches_platform(
    const std::array<std::byte, descriptor::kNetAddrSize>& address,
    std::uint64_t platformId) noexcept {
    std::array<char, 40> prefix{};
    const int length = std::snprintf(prefix.data(), prefix.size(), "steamid:%llu#",
                                     static_cast<unsigned long long>(platformId));
    if (platformId == 0 || length <= 0
        || static_cast<std::size_t>(length) > address.size()) {
        return false;
    }
    for (int index = 0; index < length; ++index) {
        if (address[static_cast<std::size_t>(index)]
            != static_cast<std::byte>(prefix[static_cast<std::size_t>(index)])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool authenticated_player_projection_ready(const Admitted& record) noexcept {
    return record.hasPlayer && record.playerId != 0 && record.playerIdentityPresent
           && !record.playerReplacementPending;
}

void report_public_binding_refusal(const Admitted& row,bool self) noexcept {
    static std::atomic_uint reports{};
    if (reports.fetch_add(1,std::memory_order_relaxed)>=16) return;
    report(core::log::Level::warn,
        "ev=gameplay stage=membership result=unqualified_public_member session=0x%016llX "
        "self=%u platform_present=%u machine_present=%u source_present=%u incarnation_present=%u",
        static_cast<unsigned long long>(row.sessionId),self?1U:0U,
        row.joiningPlatformId?1U:0U,row.activityMemberKey?1U:0U,row.activitySource?1U:0U,
        row.activityCreatedRevision&&row.activityJoinedRevision?1U:0U);
}

void collect_session_peers(
    const Admitted& record,
    std::array<const Admitted*, kAdmittedCapacity>& sessionPeers,
    std::size_t& sessionPeerCount) noexcept {
    sessionPeers = {};
    sessionPeerCount = 0;
    sessionPeers[sessionPeerCount++] = &record;
    const std::uint64_t lobbyOwnerPlatformId = record.lobbyOwnerPlatformId;
    const bool recipientIsLobbyOwner =
        lobbyOwnerPlatformId != 0 && record.joiningPlatformId == lobbyOwnerPlatformId;
    const bool publicRegion = source_activity_session(record.sessionId) != state::activity::kAbsentSessionId;
    for (const Admitted& candidate : g_admitted) {
        if (publicRegion && candidate.occupied && candidate.sessionId==record.sessionId
            && candidate.lobbyId==record.lobbyId
            && (candidate.endpoint.address!=record.endpoint.address || candidate.endpoint.port!=record.endpoint.port)
            && !public_membership_binding::ready(candidate)) {
            report_public_binding_refusal(candidate,false);
            continue;
        }
        if (!candidate.occupied || candidate.sessionId != record.sessionId
            || candidate.lobbyId != record.lobbyId
            || (candidate.endpoint.address == record.endpoint.address
                 && candidate.endpoint.port == record.endpoint.port)
            // the owner's own source must consume its seeded-row replacement; other recipients keep the
            // synthetic owner row until ID34 is ready, or the native card cache is poisoned and rows alias
            || (!recipientIsLobbyOwner
                && candidate.joiningPlatformId == lobbyOwnerPlatformId
                && !authenticated_player_projection_ready(candidate))
            || sessionPeerCount >= wire::kMemberCapacity - 1) {
            continue;
        }
        sessionPeers[sessionPeerCount++] = &candidate;
    }
    std::sort(sessionPeers.begin(),
              sessionPeers.begin() + sessionPeerCount,
              [lobbyOwnerPlatformId](const Admitted* left, const Admitted* right) noexcept {
                  const bool leftOwner = left->joiningPlatformId == lobbyOwnerPlatformId;
                  const bool rightOwner = right->joiningPlatformId == lobbyOwnerPlatformId;
                  if (leftOwner != rightOwner) {
                      return leftOwner;
                  }
                  if (left->joiningPlatformId != right->joiningPlatformId) {
                      return left->joiningPlatformId < right->joiningPlatformId;
                  }
                  if (left->endpoint.address != right->endpoint.address) {
                      return left->endpoint.address < right->endpoint.address;
                  }
                  return left->endpoint.port < right->endpoint.port;
              });
}

[[nodiscard]] bool authoritative_member_index(const Admitted& record,
                                              std::uint8_t& memberIndex) noexcept {
    std::array<const Admitted*, kAdmittedCapacity> sessionPeers{};
    std::size_t sessionPeerCount = 0;
    collect_session_peers(record, sessionPeers, sessionPeerCount);
    const bool coalescedOwner = record.lobbyId != 0 && record.lobbyOwnerPlatformId != 0;
    std::size_t nextNonOwnerIndex = 1;
    for (std::size_t index = 0; index < sessionPeerCount; ++index) {
        const Admitted& candidate = *sessionPeers[index];
        const std::size_t logicalIndex =
            coalescedOwner
                ? candidate.joiningPlatformId == record.lobbyOwnerPlatformId
                      ? kHostMemberIndex
                      : nextNonOwnerIndex++
                : index + 1;
        if (sessionPeers[index] == &record && logicalIndex < wire::kMemberCapacity) {
            memberIndex = static_cast<std::uint8_t>(logicalIndex);
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool fill_membership_profile(std::uint64_t platformId,
                                           wire::MembershipPlayer& player) noexcept;

[[nodiscard]] bool fill_public_membership_profile(const Admitted& candidate,
                                                 wire::MembershipPlayer& player) noexcept {
#if defined(RESTORATION_DEDICATED_SERVER)
    state::AccountState account{};
    std::uint64_t revision = 0;
    std::array<char, ::restoration::account_store::kAccountNameCapacity> displayName{};
    if (!::restoration::account_store::profile_by_platform(
            candidate.joiningPlatformId, account, revision, displayName)) {
        return false;
    }
    const auto end = std::find(displayName.begin(), displayName.end(), '\0');
    if (end == displayName.end()) {
        return false;
    }
    wire::MembershipPlayer presentation{};
    if (!fill_membership_profile(candidate.joiningPlatformId, presentation)) return false;
    return public_player_profile::populate_presented(
        player, candidate.joiningPlatformId, candidate.playerIdentityPresent,
        candidate.playerIdentity,
        std::string_view(displayName.data(), static_cast<std::size_t>(end - displayName.begin())),
        presentation);
#else
    (void)candidate;
    (void)player;
    return false;
#endif
}

[[nodiscard]] bool fill_membership_profile(std::uint64_t platformId,
                                           wire::MembershipPlayer& player) noexcept {
#if defined(RESTORATION_DEDICATED_SERVER)
    state::AccountState account{};
    std::uint64_t revision = 0;
    std::array<char, ::restoration::account_store::kAccountNameCapacity> displayName{};
    if (!::restoration::account_store::profile_by_platform(
            platformId, account, revision, displayName)) {
        return false;
    }
    const auto displayNameEnd = std::find(displayName.begin(), displayName.end(), '\0');
    if (displayNameEnd == displayName.begin()
        || !wire::set_ascii_membership_display_name(
            player,
            std::string_view(displayName.data(),
                             static_cast<std::size_t>(displayNameEnd - displayName.begin())))) {
        return false;
    }
    const std::uint64_t selected = state::account::selected_character_soid(account);
    const std::uint64_t presented =
        selected != 0 ? selected : state::account::banner_character_soid(account);
    std::size_t characterIndex = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].soid == presented) {
            characterIndex = index;
            break;
        }
    }
    constexpr std::size_t kEmblemSlot =
        static_cast<std::size_t>(state::account::inventory::EquipmentSlot::emblem);
    if (presented == 0 || characterIndex >= account.characterCount
        || kEmblemSlot >= account.characters[characterIndex].equipment.slots.size()) {
        return false;
    }
    const auto& equippedEmblem =
        account.characters[characterIndex].equipment.slots[kEmblemSlot];
    state::build_data::items::Definition emblem{};
    std::int32_t light = 0;
    if (!equippedEmblem.has_value()
        || !state::build_data::items::find_hash(equippedEmblem->definitionHash, emblem)
        || !state::equipment::light::resolution::character_light(
            account, characterIndex, light)
        || light < 0) {
        return false;
    }
    player.profilePresent = true;
    player.emblemDefinitionIndex = emblem.definitionIndex;
    player.profileType = kOrdinaryPlayerProfileType;
    player.light = light;
    if (player.profileSupplement == wire::PlayerProfileSupplement{}) {
        if (account.primarySoid == 0) {
            return false;
        }
        player.profileSupplement = wire::make_account_membership_profile_supplement(
            account.primarySoid,
            presented,
            std::bit_cast<std::uint32_t>(static_cast<float>(light)));
    }
    return true;
#else
    (void)platformId;
    (void)player;
    return false;
#endif
}

// a later real owner ID-34 gets its own allocator value; reusing the synthetic row's generation
// isn't a valid native remove/add history
[[nodiscard]] bool synthetic_owner_addition(PlayerAdditionState& state,
                                            std::uint64_t ownerPlatformId,
                                            std::uint32_t& additionNumber) noexcept {
    if (ownerPlatformId == 0) {
        return false;
    }
    if (!state.syntheticOwnerAdditionNumberPresent
        || state.syntheticOwnerPlatformId != ownerPlatformId) {
        state.syntheticOwnerPlatformId = ownerPlatformId;
        state.syntheticOwnerAdditionNumber = allocate_player_addition(state);
        state.syntheticOwnerAdditionNumberPresent = true;
    }
    additionNumber = state.syntheticOwnerAdditionNumber;
    return true;
}

[[nodiscard]] bool publish_snapshot(const Admitted& record) noexcept {
    if (source_activity_session(record.sessionId) != state::activity::kAbsentSessionId
        && !public_membership_binding::ready(record)) {
        report_public_binding_refusal(record,true);
        return false;
    }
    PlayerAdditionState* const additionState = claim_player_additions(record.sessionId);
    if (additionState == nullptr) {
        return false;
    }
    std::array<const Admitted*, kAdmittedCapacity> sessionPeers{};
    std::size_t sessionPeerCount = 0;
    collect_session_peers(record, sessionPeers, sessionPeerCount);
    const std::uint64_t lobbyOwnerPlatformId = record.lobbyOwnerPlatformId;
    // public joins keep an empty account/character association (Codec-A field 5) but still need the
    // platform identity (field 1) for peer account translation
    const bool publishesPlayerProfiles =
        source_activity_session(record.sessionId) == state::activity::kAbsentSessionId;

    std::array<wire::MembershipMember, wire::kMemberCapacity> members{};
    std::array<wire::MembershipPlayer, wire::kPlayerCapacity> players{};
    const state::gameplay::Endpoint host = endpoint::advertised();
    wire::MembershipMember& logicalOwnerMember = members[kHostMemberIndex];
    descriptor::write_net_addr(host.address, host.port, logicalOwnerMember.address);
    logicalOwnerMember.machineId = record.sessionId;
    logicalOwnerMember.joinId = record.joinId;
    logicalOwnerMember.state =
        record.joinComplete ? wire::MemberState::established : kJoinMemberState;
    logicalOwnerMember.connectionPresent = true;

    const bool coalescedOwner = record.lobbyId != 0 && lobbyOwnerPlatformId != 0;
    const Admitted* admittedLobbyOwner = nullptr;
    if (coalescedOwner) {
        const auto owner = std::find_if(
            sessionPeers.begin(),
            sessionPeers.begin() + sessionPeerCount,
            [lobbyOwnerPlatformId](const Admitted* candidate) noexcept {
                return candidate->joiningPlatformId == lobbyOwnerPlatformId;
            });
        if (owner != sessionPeers.begin() + sessionPeerCount) {
            admittedLobbyOwner = *owner;
        }
    }

    std::size_t memberCount = 1;
    std::size_t playerCount = 0;
    if (admittedLobbyOwner != nullptr) {
        logicalOwnerMember.joinId = admittedLobbyOwner->joinId;
        logicalOwnerMember.state = admittedLobbyOwner->joinComplete
                                       ? wire::MemberState::established
                                       : kJoinMemberState;
        if (admittedLobbyOwner->hasPlayer) {
            if (!admittedLobbyOwner->membershipAdditionNumberPresent) {
                return false;
            }
            wire::MembershipPlayer& owner = players[playerCount];
            owner.slot = static_cast<std::uint32_t>(playerCount);
            owner.playerId = admittedLobbyOwner->playerId;
            owner.memberIndex = kHostMemberIndex;
            owner.addSequence = admittedLobbyOwner->membershipAdditionNumber;
            owner.identityPresent = admittedLobbyOwner->playerIdentityPresent;
            owner.identity = admittedLobbyOwner->playerIdentity;
            owner.profileSupplement = admittedLobbyOwner->playerProfileSupplement;
            if (!fill_membership_profile(lobbyOwnerPlatformId, owner)) {
                report(core::log::Level::debug,
                       "ev=gameplay stage=membership result=deferred reason=owner_profile "
                       "session=0x%016llX platform=0x%016llX",
                       static_cast<unsigned long long>(record.sessionId),
                       static_cast<unsigned long long>(lobbyOwnerPlatformId));
                return false;
            }
            logicalOwnerMember.ownsPlayerSlot = true;
            logicalOwnerMember.playerSlot = owner.slot;
            ++playerCount;
        }
    } else if (coalescedOwner) {
        logicalOwnerMember.joinId = record.sessionId;
        // a joiner can arrive before the owner's endpoint; publish the lobby-authenticated owner on member
        // zero until its ID36 -> ID34 replacement lands, then replace in place instead of a third member
        wire::MembershipPlayer& owner = players[playerCount];
        owner.slot = static_cast<std::uint32_t>(playerCount);
        owner.playerId = lobbyOwnerPlatformId;
        owner.memberIndex = kHostMemberIndex;
        if (!synthetic_owner_addition(
                *additionState, lobbyOwnerPlatformId, owner.addSequence)) {
            return false;
        }
        if (!wire::set_synthesized_pc_platform_identity(owner, lobbyOwnerPlatformId)
            || !fill_membership_profile(lobbyOwnerPlatformId, owner)) {
            report(core::log::Level::debug,
                   "ev=gameplay stage=membership result=deferred reason=owner_profile "
                   "session=0x%016llX platform=0x%016llX",
                   static_cast<unsigned long long>(record.sessionId),
                   static_cast<unsigned long long>(lobbyOwnerPlatformId));
            return false;
        }
        logicalOwnerMember.ownsPlayerSlot = true;
        logicalOwnerMember.playerSlot = owner.slot;
        ++playerCount;
    }
    std::uint32_t successionIndex = kHostMemberIndex;
    for (std::size_t peerIndex = 0; peerIndex < sessionPeerCount; ++peerIndex) {
        const Admitted& candidate = *sessionPeers[peerIndex];
        if (coalescedOwner && candidate.joiningPlatformId == lobbyOwnerPlatformId) {
            continue;
        }
        const std::size_t memberIndex = memberCount++;
        wire::MembershipMember& member = members[memberIndex];
        member_address_for_recipient(candidate, record, member.address);
        member.machineId = member_machine_id(candidate);
        member.joinId = candidate.joinId;
        member.state = candidate.joinComplete ? wire::MemberState::established : kJoinMemberState;
        // public Citizen sessions use the archived field-10-only member/player relationship; central
        // fireteams keep the recovered scalar field 9
        member.ownedPlayerCountPresent = publishesPlayerProfiles;
        member.connectionPresent = &candidate == &record;
        // a public ActivityClient has no managed net-session, so its player link must share the
        // public group's channel; roster-only rows left arrivals stuck in securing2
        if (!publishesPlayerProfiles && &candidate != &record) {
            std::array<std::byte, descriptor::kNetAddrSize> publicAddress{};
            if (public_peer_address(candidate, publicAddress)) {
                member.address = publicAddress;
                member.connectionPresent = true;
            }
        }
        if (!coalescedOwner && publishesPlayerProfiles
            && successionIndex == kHostMemberIndex) {
            successionIndex = static_cast<std::uint32_t>(memberIndex);
        }
        if (candidate.hasPlayer && playerCount < players.size()) {
            if (!candidate.membershipAdditionNumberPresent) {
                return false;
            }
            wire::MembershipPlayer& player = players[playerCount];
            player.slot = static_cast<std::uint32_t>(playerCount);
            player.playerId = candidate.playerId;
            player.memberIndex = static_cast<std::uint32_t>(memberIndex);
            player.addSequence = candidate.membershipAdditionNumber;
            if (!publishesPlayerProfiles) {
                if (!fill_public_membership_profile(candidate, player)) {
                    report(core::log::Level::warn,
                           "ev=gameplay stage=membership result=deferred reason=public_name "
                           "platform=0x%016llX",
                           static_cast<unsigned long long>(candidate.joiningPlatformId));
                    return false;
                }
            } else {
                if (candidate.playerIdentityPresent) {
                    player.identityPresent = true;
                    player.identity = candidate.playerIdentity;
                } else if (!wire::set_synthesized_pc_platform_identity(
                               player, candidate.joiningPlatformId)) {
                    return false;
                }
                player.profileSupplement = candidate.playerProfileSupplement;
                if (!fill_membership_profile(candidate.joiningPlatformId, player)) {
                    report(core::log::Level::debug,
                           "ev=gameplay stage=membership result=deferred reason=player_profile "
                           "session=0x%016llX platform=0x%016llX player=0x%016llX",
                           static_cast<unsigned long long>(record.sessionId),
                           static_cast<unsigned long long>(candidate.joiningPlatformId),
                           static_cast<unsigned long long>(candidate.playerId));
                    return false;
                }
            }
            // the PC 1.59 encoder needs this ownership edge to finish local-player sync; dropping it stalls
            // slice-set loading before the PUBLIC ActivityClient can send its join request
            member.ownsPlayerSlot = true;
            member.playerSlot = player.slot;
            ++playerCount;
        }
    }

    wire::MembershipUpdate update{};
    update.hostMachineId = record.sessionId;
    // revisions belong to the native session, not the process; spending them on the current group made
    // the target's first snapshot start at five, which PC 1.59 showed but never committed
    update.revision = ++additionState->membershipRevision;
    update.hostMemberIndex = kHostMemberIndex;
    update.successionIndex = successionIndex;
    update.members = std::span<const wire::MembershipMember>(members.data(), memberCount);
    update.players = std::span<const wire::MembershipPlayer>(players.data(), playerCount);
    update.nextPlayerAdditionNumber = additionState->nextAdditionNumber;
    update.playerAdditionNumberPresent = publishesPlayerProfiles;

    std::array<std::byte, kMembershipBodyCapacity> body{};
    bits::Writer writer(body);
    std::size_t size = 0;
    if (!wire::write_membership_update(writer, update) || !writer.finish(size)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=membership result=encode_fail members=%zu players=%zu",
               memberCount,
               playerCount);
        return false;
    }
    const std::uint32_t stateHash = wire::session_state_hash(update);
    dump_membership_projection(update,
                               {body.data(), size},
                               writer.bit_count(),
                               stateHash);
    report(core::log::Level::info,
           "ev=gameplay stage=membership result=built revision=%u members=%zu players=%zu "
           "hash=0x%08X target=0x%016llX lobby=0x%016llX owner=0x%016llX "
           "joiner=0x%016llX next_player_add=%u profiles=%s",
           update.revision,
           memberCount,
           playerCount,
           stateHash,
           static_cast<unsigned long long>(member_machine_id(record)),
           static_cast<unsigned long long>(record.lobbyId),
           static_cast<unsigned long long>(record.lobbyOwnerPlatformId),
           static_cast<unsigned long long>(record.joiningPlatformId),
           update.nextPlayerAdditionNumber,
           publishesPlayerProfiles ? "full" : "public-platform-name");
    return peer::enqueue_reliable_to(
        record.endpoint,
        record.sessionId,
        static_cast<std::uint8_t>(wire::SessionMessageId::membershipUpdate),
        wire::kMembershipUpdateSize,
        {body.data(), size},
        writer.bit_count());
}

void fill_activity_host(wire::ActivityHostParameter& body, std::uint64_t groupSessionId) noexcept {
    body.selectionId = 0;
    // the peer sends its activity join to this id and the route refuses one naming no committed
    // activity session; a gameplay identity isn't one
    body.hostId = held_host_session(groupSessionId);
    body.memberMask = kAllMembers;
    publish_activity_host_endpoint(body, core::settings::get());
}

[[nodiscard]] bool fill_current_activity(wire::CurrentActivityParameter& body,
                                         std::uint64_t groupSessionId) noexcept {
    const std::uint64_t sourceActivitySessionId = source_activity_session(groupSessionId);
    if (sourceActivitySessionId != state::activity::kAbsentSessionId) {
        state::activity::SessionRecord source{};
        if (!source_activity_snapshot(groupSessionId, source)
            || !state::activity::destination::valid(source.destination)) {
            return false;
        }
        const auto& destination = source.destination;
        body.reason = destination.reason;
        body.sourceActivityIndex = destination.previousActivityIndex;
        body.activityIndex = destination.activityIndex;
    } else {
        body.reason = 0;
        body.sourceActivityIndex = 0;
        body.activityIndex = 0;
    }
    body.present = true;
    body.fixedValue = 2;
    return true;
}

// parameters 7 and 8 share the 0x118-byte type; previous-activity sits at container
// offset 0x378, first dword actual-activity-index; default -1 triggers Olive, normal is 0
void fill_previous_activity(wire::PreviousActivityParameter& body) noexcept {
    body.present = true;
    body.reason = 0;
    body.sourceActivityIndex = 0;
    body.activityIndex = 0;
    body.fixedValue = 2;
}

[[nodiscard]] bool publish_activity_host_for_session(const Admitted& record,
                                                     std::uint64_t parameterSessionId) noexcept {
    if (held_host_session(parameterSessionId) == state::activity::kAbsentSessionId) {
        // a zero host id latches an unusable parameter on the peer, which reads it only once; the
        // region's advertisement allocates and this retries
        report(core::log::Level::debug, "ev=gameplay stage=activityhost result=nosession");
        return false;
    }
    wire::ParameterUpdate update{};
    update.sessionId = parameterSessionId;
    // one update so the peer never holds the host without its activity; republish the full value, a
    // carried empty root replaces the usable one and the world controller reports no activity index
    update.carriedMask =
        (std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::activityHost))
        | (std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::currentActivity));
    const bool publicRegion =
        source_activity_session(parameterSessionId) != state::activity::kAbsentSessionId;
    if (publicRegion) {
        update.carriedMask |=
            std::uint64_t{1}
            << static_cast<std::uint8_t>(wire::Parameter::previousActivity);
        fill_previous_activity(update.previousActivity);
    }
    fill_activity_host(update.activityHost, parameterSessionId);
    if (!fill_current_activity(update.currentActivity, parameterSessionId)) {
        report(core::log::Level::debug,
               "ev=gameplay stage=activityhost result=noactivity session=0x%016llX",
               static_cast<unsigned long long>(parameterSessionId));
        return false;
    }

    const bool sent = send_reliable(
        record.endpoint,
        record.sessionId,
        wire::kParameterUpdateId,
        wire::kParameterUpdateSize,
        [&update](bits::Writer& writer) { return wire::write_parameter_update(writer, update); });
    std::array<char, kParameterNameCapacity> names{};
    report(sent ? core::log::Level::info : core::log::Level::debug,
           "ev=gameplay stage=activityhost result=%s link=0x%016llX session=0x%016llX "
           "host=0x%llX address=0x%08X port=%u activity=%d->%d reason=%d names=%s",
           sent ? "queued" : "deferred",
           static_cast<unsigned long long>(record.sessionId),
           static_cast<unsigned long long>(parameterSessionId),
           static_cast<unsigned long long>(update.activityHost.hostId),
           update.activityHost.address,
           static_cast<unsigned>(update.activityHost.port),
           static_cast<int>(update.currentActivity.sourceActivityIndex),
           static_cast<int>(update.currentActivity.activityIndex),
           static_cast<int>(update.currentActivity.reason),
           wire::parameter_names(update.carriedMask, names.data(), names.size()));
    return sent;
}

// kind 25 means the peer reopened its channels; the migration ladder then waits for a newer
// membership revision. Keep the generation until its snapshot reaches the reliable queue
[[nodiscard]] bool publish_peer_reestablish_membership(Admitted& record) noexcept {
    if (record.peerReestablishMembershipGeneration >= record.peerReestablishGeneration) {
        return true;
    }
    const std::uint64_t generation = record.peerReestablishGeneration;
    const bool queued = publish_snapshot(record);
    if (queued) {
        record.peerReestablishMembershipGeneration = generation;
    }
    report(queued ? core::log::Level::info : core::log::Level::debug,
           "ev=gameplay stage=peer_reestablish result=%s session=0x%016llX "
           "generation=%llu membership_generation=%llu",
           queued ? "membership_queued" : "membership_deferred",
           static_cast<unsigned long long>(record.sessionId),
           static_cast<unsigned long long>(generation),
           static_cast<unsigned long long>(record.peerReestablishMembershipGeneration));
    return queued;
}

void resolve_host_member(const Admitted& record,
                         std::uint64_t& machineId,
                         std::uint64_t& identitySource,
                         std::array<std::byte, descriptor::kNetAddrSize>& address) noexcept {
    const state::gameplay::Endpoint host = endpoint::advertised();
    identitySource = record.sessionId;
    machineId = record.sessionId;
    descriptor::write_net_addr(host.address, host.port, address);
}

// public region ids are unrelated to Steam lobby ids, but the record's platform resolves
// the central lobby whose owner descriptor is the exact 128-byte retail value
[[nodiscard]] bool populate_native_host_identity(
    const Admitted& record,
    wire::HostReestablish& body,
    std::uint64_t& lobbyId,
    std::uint64_t& ownerPlatformId,
    std::uint64_t& nativeMachineId) noexcept {
    lobbyId = 0;
    ownerPlatformId = 0;
    nativeMachineId = 0;
#if defined(RESTORATION_DEDICATED_SERVER)
    middleware::social::realtime::Lobby lobby{};
    ::restoration::realtime_store::GameplayBootstrapDescriptor nativeDescriptor{};
    if (record.joiningPlatformId == 0
        || !::restoration::realtime_store::lobby_for_member(record.joiningPlatformId, lobby)
        || lobby.id == 0 || lobby.ownerPlatformId == 0
        || !::restoration::realtime_store::native_fireteam_descriptor(
            lobby.id, lobby.ownerPlatformId, nativeDescriptor)) {
        return false;
    }

    std::copy_n(nativeDescriptor.begin() + kNativeIdentity128Offset,
                body.identity128.size(),
                body.identity128.begin());
    std::copy_n(nativeDescriptor.begin() + kNativeIdentity144Offset,
                body.identity144.size(),
                body.identity144.begin());
    const bool identity128Present = std::any_of(
        body.identity128.begin(), body.identity128.end(), [](std::byte value) noexcept {
            return value != std::byte{};
        });
    const bool identity144Present = std::any_of(
        body.identity144.begin(), body.identity144.end(), [](std::byte value) noexcept {
            return value != std::byte{};
        });
    if (!identity128Present || !identity144Present) {
        body.identity128 = {};
        body.identity144 = {};
        return false;
    }

    lobbyId = lobby.id;
    ownerPlatformId = lobby.ownerPlatformId;
    nativeMachineId = identity_word(nativeDescriptor, 0);
    return nativeMachineId != 0;
#else
    (void)record;
    (void)body;
    return true;
#endif
}

[[nodiscard]] bool publish_activity_host(const Admitted& record) noexcept {
    return publish_activity_host_for_session(record, record.sessionId);
}

// the manager resolves this packet to member record 0; machineId is not the record selector
[[nodiscard]] bool publish_host_reestablish(const Admitted& record) noexcept {
    wire::HostReestablish body{};
    std::uint64_t identitySource = 0;
    std::uint64_t identityLobbyId = 0;
    std::uint64_t identityOwnerPlatformId = 0;
    std::uint64_t nativeMachineId = 0;
    body.sessionId = record.sessionId;
    resolve_host_member(record, body.machineId, identitySource, body.address);
    if (!populate_native_host_identity(record,
                                       body,
                                       identityLobbyId,
                                       identityOwnerPlatformId,
                                       nativeMachineId)) {
        report(core::log::Level::debug,
               "ev=gameplay stage=host_reestablish phase=baseline result=deferred "
               "reason=native_identity session=0x%016llX platform=0x%016llX",
               static_cast<unsigned long long>(body.sessionId),
               static_cast<unsigned long long>(record.joiningPlatformId));
        return false;
    }

    const bool sent =
        send_reliable(record.endpoint,
                      record.sessionId,
                      static_cast<std::uint8_t>(wire::SessionMessageId::hostReestablish),
                      wire::kHostReestablishSize,
                      [&body](bits::Writer& writer) noexcept {
                          return wire::write_host_reestablish(writer, body);
                      });
    report(sent ? core::log::Level::info : core::log::Level::debug,
           "ev=gameplay stage=host_reestablish phase=baseline result=%s session=0x%016llX "
           "machine=0x%016llX target_member=%u identity_source=0x%016llX decoded_size=%u "
           "descriptor=dedicated_hybrid identity_lobby=0x%016llX "
           "identity_owner=0x%016llX native_machine=0x%016llX address_method=%u",
           sent ? "queued" : "deferred",
           static_cast<unsigned long long>(body.sessionId),
           static_cast<unsigned long long>(body.machineId),
           static_cast<unsigned>(kHostMemberIndex),
           static_cast<unsigned long long>(identitySource),
           wire::kHostReestablishSize,
           static_cast<unsigned long long>(identityLobbyId),
           static_cast<unsigned long long>(identityOwnerPlatformId),
           static_cast<unsigned long long>(nativeMachineId),
           static_cast<unsigned>(body.address[descriptor::kNetAddrSize - 1]));
    return sent;
}

void bind_view(const state::gameplay::Endpoint& from,
               std::uint64_t sessionId,
               const wire::ViewEstablishment& view) noexcept {
    const std::uint64_t routedSessionId = resolve_view_session(from, sessionId);
    NativeActivityBinding activityBinding{};
    if (native_activity_binding(from, routedSessionId, activityBinding)) {
        std::array<bap::NativeActivityView, 3> views{};
        std::size_t count{};
        const bool captured = bap::snapshot_native_activity_views(activityBinding.account, views, count);
        const auto hostActivity = held_host_session(routedSessionId);
        const auto end = views.begin() + count;
        const auto exact = std::find_if(views.begin(), end, [&](const auto& candidate) {
            if (candidate.activity != view.sessionToken || candidate.member != activityBinding.member)
                return false;
            if (candidate.activity == activityBinding.sourceActivity)
                return candidate.createdRevision == activityBinding.createdRevision
                    && candidate.joinedRevision == activityBinding.joinedRevision;
            return candidate.activity == hostActivity;
        });
        if (!captured || exact == end) {
            static std::atomic<unsigned> refusals{};
            if (refusals.fetch_add(1) < 16) report(core::log::Level::warn,
                "ev=gameplay stage=native_view_identity result=refused token=0x%llX captured=%u count=%zu",
                static_cast<unsigned long long>(view.sessionToken), static_cast<unsigned>(captured), count);
            return;
        }
        static std::atomic<unsigned> observations{};
        if (observations.fetch_add(1) < 16) report(core::log::Level::info,
            "ev=gameplay stage=native_view_identity result=matched activity=0x%llX account=0x%llX "
            "connection=%u incarnation=%llu join=%llu common=%016llX:%016llX",
            static_cast<unsigned long long>(exact->activity), static_cast<unsigned long long>(exact->account),
            exact->connectionId, static_cast<unsigned long long>(exact->connectionIncarnation),
            static_cast<unsigned long long>(exact->joinedRevision),
            static_cast<unsigned long long>(exact->commonIdentity[0]),
            static_cast<unsigned long long>(exact->commonIdentity[1]));
    }
    state::gameplay::ViewSignature signature{};
    signature.token = view.sessionToken;
    signature.kind = view.kind;
    signature.listCount = view.listCount;
    signature.hasList = view.hasList;
    signature.list = view.list;
    signature.bound = true;
    peer::bind_view_for(from, routedSessionId, signature);

    const bool sent = routedSessionId != 0 && send_reliable(
        from,
        routedSessionId,
        wire::kViewMessageId,
        wire::kViewMessageSize,
        [&view](bits::Writer& writer) noexcept { return wire::write_view(writer, view); });
    const char* const result = sent ? "bound" : routedSessionId == 0 ? "unresolved" : "deferred";
    report(sent ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=view result=%s route=0x%016llX multiplexed=%u kind=%u "
           "token=0x%llX list=%u",
           result,
           static_cast<unsigned long long>(routedSessionId),
           static_cast<unsigned>(sessionId == 0 ? 1U : 0U),
           static_cast<unsigned>(view.kind),
           static_cast<unsigned long long>(view.sessionToken),
           static_cast<unsigned>(view.listCount));
}

[[nodiscard]] bool fill_parameter_authority_arrays(
    const Admitted& target,
    const Admitted* replacementSource,
    const wire::EncodedParameterBody* replacementSelection,
    const wire::EncodedParameterBody* replacementResponses,
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity>& selections,
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity>& responses,
    std::size_t& sessionPeerCount) noexcept {
    std::array<const Admitted*, kAdmittedCapacity> sessionPeers{};
    collect_session_peers(target, sessionPeers, sessionPeerCount);
    selections = {};
    responses = {};
    const bool coalescedOwner = target.lobbyId != 0 && target.lobbyOwnerPlatformId != 0;
    std::size_t nextNonOwnerIndex = 1;
    for (std::size_t peerIndex = 0; peerIndex < sessionPeerCount; ++peerIndex) {
        const Admitted& source = *sessionPeers[peerIndex];
        const std::size_t memberIndex =
            coalescedOwner
                ? source.joiningPlatformId == target.lobbyOwnerPlatformId
                      ? kHostMemberIndex
                      : nextNonOwnerIndex++
                : peerIndex + 1;
        if (memberIndex >= selections.size()) {
            return false;
        }
        const wire::EncodedParameterBody& selection =
            &source == replacementSource && replacementSelection != nullptr
                ? *replacementSelection
                : source.activitySelectionProposal;
        const wire::EncodedParameterBody& response =
            &source == replacementSource && replacementResponses != nullptr
                ? *replacementResponses
                : source.activitySelectionResponsesProposal;
        if (selection.present) {
            selections[memberIndex] = &selection;
        }
        if (response.present) {
            responses[memberIndex] = &response;
        }
    }
    return true;
}

[[nodiscard]] bool parameter_authority_snapshot_fits(
    const Admitted& source,
    const wire::EncodedParameterBody& selection,
    const wire::EncodedParameterBody& response,
    std::size_t& bodyBits) noexcept {
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity> selections{};
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity> responses{};
    std::size_t sessionPeerCount = 0;
    if (!fill_parameter_authority_arrays(source,
                                         &source,
                                         &selection,
                                         &response,
                                         selections,
                                         responses,
                                         sessionPeerCount)) {
        return false;
    }
    wire::ParameterUpdate update{};
    update.sessionId = source.sessionId;
    update.carriedMask = kActivitySelectionAuthorityMask;
    update.activitySelections = selections;
    update.activitySelectionResponses = responses;
    bits::Writer measure = bits::Writer::measuring();
    std::size_t ignoredBytes = 0;
    if (!wire::write_parameter_update(measure, update) || !measure.finish(ignoredBytes)) {
        return false;
    }
    bodyBits = measure.bit_count();
    return bodyBits <= kMaximumReliableBodyBits;
}

[[nodiscard]] bool publish_parameter_authority_snapshot(Admitted& target,
                                                        std::uint64_t carriedMask) noexcept {
    carriedMask &= kActivitySelectionAuthorityMask;
    if (carriedMask == 0) {
        return false;
    }
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity> selections{};
    std::array<const wire::EncodedParameterBody*, wire::kParameterMemberCapacity> responses{};
    std::size_t sessionPeerCount = 0;
    if (!fill_parameter_authority_arrays(target,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         selections,
                                         responses,
                                         sessionPeerCount)) {
        return false;
    }

    wire::ParameterUpdate update{};
    update.sessionId = target.sessionId;
    update.carriedMask = carriedMask;
    update.activitySelections = selections;
    update.activitySelectionResponses = responses;
    const bool queued = send_reliable_sized<kParameterAuthorityBodyCapacity>(
        target.endpoint,
        target.sessionId,
        wire::kParameterUpdateId,
        wire::kParameterUpdateSize,
        [&update](bits::Writer& writer) noexcept {
            return wire::write_parameter_update(writer, update);
        });
    report(queued ? core::log::Level::info : core::log::Level::debug,
           "ev=gameplay stage=parameter_authority result=%s session=0x%016llX "
           "target=0x%016llX peers=%zu carried=0x%08X released=0x%08X",
           queued ? "queued" : "deferred",
           static_cast<unsigned long long>(target.sessionId),
           static_cast<unsigned long long>(member_machine_id(target)),
           sessionPeerCount,
           static_cast<unsigned>(update.carriedMask),
           static_cast<unsigned>(update.releasedMask));
    return queued;
}

void apply_parameter_authority(const state::gameplay::Endpoint& from,
                               std::uint64_t sessionId,
                               bool modeFlag,
                               std::uint64_t suppliedMask,
                               wire::EncodedParameterBody selection,
                               wire::EncodedParameterBody responses) noexcept {
    suppliedMask &= kActivitySelectionAuthorityMask;
    const std::uint64_t selectionBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::activitySelection);
    const std::uint64_t responsesBit =
        std::uint64_t{1}
        << static_cast<std::uint8_t>(wire::Parameter::activitySelectionResponses);
    const bool selectionSupplied = (suppliedMask & selectionBit) != 0;
    const bool responsesSupplied = (suppliedMask & responsesBit) != 0;
    const int selectionWireMember =
        selectionSupplied && selection.memberIndexPresent
            && selection.memberIndex != wire::kUnspecifiedParameterMember
            ? static_cast<int>(selection.memberIndex)
            : selectionSupplied ? -1 : -2;
    const int responsesWireMember =
        responsesSupplied && responses.memberIndexPresent
            && responses.memberIndex != wire::kUnspecifiedParameterMember
            ? static_cast<int>(responses.memberIndex)
            : responsesSupplied ? -1 : -2;

    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* const record = find_admitted(from, sessionId);
    const bool sourceIsLogicalOwner =
        record != nullptr && record->lobbyOwnerPlatformId != 0
        && record->joiningPlatformId == record->lobbyOwnerPlatformId;
    // parameter 5 is the leader's selected activity; if a joiner could replace it the joiner would appear
    // to be host and the real leader couldn't launch. Every member may still send its parameter-6 row
    const bool acceptedSelectionSupplied = selectionSupplied && sourceIsLogicalOwner;
    const std::uint64_t acceptedMask =
        (acceptedSelectionSupplied ? selectionBit : 0)
        | (responsesSupplied ? responsesBit : 0);
    wire::EncodedParameterBody mergedSelection = selection;
    wire::EncodedParameterBody mergedResponses = responses;
    if (record != nullptr) {
        if (!acceptedSelectionSupplied) {
            mergedSelection = record->activitySelectionProposal;
        }
        if (!responsesSupplied) {
            mergedResponses = record->activitySelectionResponsesProposal;
        }
    }
    std::uint8_t memberIndex = 0;
    const bool authorized = record != nullptr && record->joinComplete
                            && authoritative_member_index(*record, memberIndex);
    const bool memberBound =
        authorized && acceptedMask != 0
        && (!acceptedSelectionSupplied
            || wire::validate_parameter_proposal_member(selection, memberIndex))
        && (!responsesSupplied
            || wire::validate_parameter_proposal_member(responses, memberIndex));
    std::size_t aggregateBits = 0;
    const bool aggregateFits =
        memberBound
        && parameter_authority_snapshot_fits(
            *record, mergedSelection, mergedResponses, aggregateBits);
    const bool bound = memberBound && aggregateFits;
    std::uint64_t changedMask = 0;
    std::size_t queued = 0;
    std::size_t owed = 0;
    if (bound) {
        if (acceptedSelectionSupplied
            && !wire::same_parameter_body(record->activitySelectionProposal, selection)) {
            record->activitySelectionProposal = selection;
            changedMask |= selectionBit;
        }
        if (responsesSupplied
            && !wire::same_parameter_body(record->activitySelectionResponsesProposal,
                                          responses)) {
            record->activitySelectionResponsesProposal = responses;
            changedMask |= responsesBit;
        }
        if (changedMask != 0) {
            for (Admitted& target : g_admitted) {
                if (target.occupied && target.sessionId == record->sessionId
                    && target.lobbyId == record->lobbyId) {
                    target.parameterAuthorityDirtyMask |= changedMask;
                }
            }
        }
        // reliable delivery already retries; re-answering an unchanged proposal makes each client treat the
        // echo as new and resend ID-39, an endless ID-39/ID-38 loop that starves membership and leave
        for (Admitted& target : g_admitted) {
            if (!target.occupied || target.sessionId != record->sessionId
                || target.lobbyId != record->lobbyId) {
                continue;
            }
            const std::uint64_t pending =
                target.parameterAuthorityDirtyMask & kActivitySelectionAuthorityMask;
            if (pending == 0) {
                continue;
            }
            ++owed;
            if (publish_parameter_authority_snapshot(target, pending)) {
                target.parameterAuthorityDirtyMask &= ~pending;
                ++queued;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);

    report(bound ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=parameter_authority result=%s session=0x%016llX mode=%u "
           "requested=0x%08X accepted=0x%08X leader=%u changed_mask=0x%08X member=%u "
           "selection_wire_member=%d responses_wire_member=%d "
           "selection_bits=%zu responses_bits=%zu aggregate_bits=%zu aggregate_limit=%zu "
           "queued=%zu owed=%zu",
           !authorized ? "unauthorized"
           : !memberBound ? "bind_rejected"
           : !aggregateFits ? "aggregate_oversize"
           : queued != 0
               ? "published"
           : owed != 0 ? "retry_armed"
                       : "held",
           static_cast<unsigned long long>(sessionId),
           static_cast<unsigned>(modeFlag ? 1U : 0U),
           static_cast<unsigned>(suppliedMask),
           static_cast<unsigned>(acceptedMask),
           static_cast<unsigned>(sourceIsLogicalOwner ? 1U : 0U),
           static_cast<unsigned>(changedMask),
           static_cast<unsigned>(memberIndex),
           selectionWireMember,
           responsesWireMember,
           selectionSupplied ? selection.bitCount : 0,
           responsesSupplied ? responses.bitCount : 0,
           aggregateBits,
           kMaximumReliableBodyBits,
           queued,
           owed);
}

void answer_parameters(const state::gameplay::Endpoint& from,
                       std::uint64_t sessionId,
                       std::uint64_t requested) noexcept {
    std::uint64_t carried =
        requested & wire::kEncodableParameters & ~kActivitySelectionAuthorityMask;
    const std::uint64_t activityHostBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::activityHost);
    const std::uint64_t currentActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::currentActivity);
    const std::uint64_t previousActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::previousActivity);
    // only an activity-host request may claim a host slot; a network-quality-only request during a
    // public-instance swap created a destination-less session the native swap later dereferenced
    bool claimedHostSlot = false;
    const std::uint64_t sourceActivitySessionId = state::activity::newest_joined_session();
    if ((carried & activityHostBit) != 0
        && activity_host_session(
               sessionId, kUnknownRegion, sourceActivitySessionId, claimedHostSlot)
               == state::activity::kAbsentSessionId) {
        carried &= ~activityHostBit;
    }
    wire::CurrentActivityParameter currentActivity{};
    if ((carried & currentActivityBit) != 0
        && !fill_current_activity(currentActivity, sessionId)) {
        // a carried empty root erases a concrete current activity the peer already holds, so leave it
        // unanswered until the source destination is available
        carried &= ~currentActivityBit;
    }
    wire::PreviousActivityParameter previousActivity{};
    if ((carried & previousActivityBit) != 0) {
        fill_previous_activity(previousActivity);
    }
    if (carried == 0) {
        report(core::log::Level::debug,
               "ev=gameplay stage=parameters result=unheld mask=0x%08X",
               static_cast<unsigned>(requested));
        return;
    }

    wire::ParameterUpdate update{};
    update.sessionId = sessionId;
    update.carriedMask = carried;
    fill_activity_host(update.activityHost, sessionId);
    update.currentActivity = currentActivity;
    update.previousActivity = previousActivity;

    const bool sent = send_reliable(
        from,
        sessionId,
        wire::kParameterUpdateId,
        wire::kParameterUpdateSize,
        [&update](bits::Writer& writer) { return wire::write_parameter_update(writer, update); });
    std::array<char, kParameterNameCapacity> names{};
    report(sent ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=parameters result=%s carried=0x%08X names=%s",
           sent ? "answered" : "fail",
           static_cast<unsigned>(carried),
           wire::parameter_names(carried, names.data(), names.size()));
}

void answer_time(const state::gameplay::Endpoint& from,
                 const wire::TimeSynchronize& probe) noexcept {
    if (!peer::send_out_of_band(from,
                                static_cast<std::uint8_t>(wire::SessionMessageId::timeSynchronize),
                                wire::kTimeSynchronizeSize,
                                [&probe](bits::Writer& writer) noexcept {
                                    return wire::write_time_synchronize(writer, probe);
                                })) {
        report(core::log::Level::debug, "ev=gameplay stage=time result=fail");
    }
}

void release(const state::gameplay::Endpoint& from, std::uint64_t sessionId) noexcept {
    peer::drop_for(from, sessionId);
    // the region's activity host stays; a leave is also how the peer fast travels into the region it
    // is already in, and a fresh id there is public_activity_host_mismatch
    AcquireSRWLockExclusive(&g_admittedLock);
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.sessionId == sessionId && entry.endpoint.address == from.address
            && entry.endpoint.port == from.port) {
            entry = {};
        }
    }
    clear_parameter_authority(sessionId);
    mark_other_members_dirty(sessionId, from);
    release_player_additions_if_unused(sessionId);
    ReleaseSRWLockExclusive(&g_admittedLock);
}

#if defined(RESTORATION_DEDICATED_SERVER)
struct CentralMembershipProbe final {
    state::gameplay::Endpoint endpoint{};
    std::uint64_t sessionId{};
    std::uint64_t lobbyId{};
    std::uint64_t platformId{};
};

// a Steam-lobby leave can finish before Destiny's reliable ID-15 arrives; waiting left
// the player visible. The central lobby is authoritative, so tear down the old session now
void reconcile_central_membership() noexcept {
    std::array<CentralMembershipProbe, kAdmittedCapacity> probes{};
    std::size_t probeCount = 0;
    AcquireSRWLockShared(&g_admittedLock);
    for (const Admitted& record : g_admitted) {
        if (!record.occupied || record.sessionId == 0 || record.lobbyId == 0
            || record.joiningPlatformId == 0 || probeCount >= probes.size()) {
            continue;
        }
        probes[probeCount++] = {
            record.endpoint, record.sessionId, record.lobbyId, record.joiningPlatformId};
    }
    ReleaseSRWLockShared(&g_admittedLock);

    for (std::size_t index = 0; index < probeCount; ++index) {
        const CentralMembershipProbe& probe = probes[index];
        middleware::social::realtime::Lobby lobby{};
        const bool lobbyPresent =
            ::restoration::realtime_store::lobby_snapshot(probe.lobbyId, lobby);
        const bool memberPresent =
            lobbyPresent
            && std::any_of(lobby.members.begin(),
                           lobby.members.begin() + lobby.memberCount,
                           [&probe](const middleware::social::realtime::Member& member) noexcept {
                               return member.platformId == probe.platformId;
                           });
        if (!memberPresent) {
            release(probe.endpoint, probe.sessionId);
            report(core::log::Level::info,
                   "ev=gameplay stage=central_membership result=reconciled_leave "
                   "session=0x%016llX lobby=0x%016llX platform=0x%016llX "
                   "endpoint=0x%08X:%u lobby_present=%u",
                   static_cast<unsigned long long>(probe.sessionId),
                   static_cast<unsigned long long>(probe.lobbyId),
                   static_cast<unsigned long long>(probe.platformId),
                   probe.endpoint.address,
                   static_cast<unsigned>(probe.endpoint.port),
                   static_cast<unsigned>(lobbyPresent ? 1U : 0U));
            continue;
        }

        bool ownerChanged = false;
        AcquireSRWLockExclusive(&g_admittedLock);
        for (Admitted& survivor : g_admitted) {
            if (!survivor.occupied || survivor.lobbyId != probe.lobbyId
                || survivor.lobbyOwnerPlatformId == lobby.ownerPlatformId) {
                continue;
            }
            survivor.lobbyOwnerPlatformId = lobby.ownerPlatformId;
            survivor.membershipDirty = true;
            ownerChanged = true;
        }
        ReleaseSRWLockExclusive(&g_admittedLock);
        if (ownerChanged) {
            report(core::log::Level::info,
                   "ev=gameplay stage=central_membership result=owner_reconciled "
                   "session=0x%016llX lobby=0x%016llX owner=0x%016llX",
                   static_cast<unsigned long long>(probe.sessionId),
                   static_cast<unsigned long long>(probe.lobbyId),
                   static_cast<unsigned long long>(lobby.ownerPlatformId));
        }
    }
}
#endif

} // namespace

void release_endpoint(const state::gameplay::Endpoint& endpoint) noexcept {
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_admittedLock);
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.endpoint.address == endpoint.address
            && entry.endpoint.port == endpoint.port) {
            const std::uint64_t sessionId = entry.sessionId;
            ++count;
            entry = {};
            clear_parameter_authority(sessionId);
            mark_other_members_dirty(sessionId, endpoint);
            release_player_additions_if_unused(sessionId);
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    if (count != 0) {
        report(core::log::Level::info,
               "ev=gameplay stage=admitted result=dropped endpoint=0x%08X:%u sessions=%zu",
               endpoint.address,
               static_cast<unsigned>(endpoint.port),
               count);
    }
}

bool consume(const state::gameplay::Endpoint& from,
             std::uint64_t sessionId,
             std::uint8_t id,
             bits::Reader& reader,
             std::uint64_t now,
             std::size_t exactBodyBitCount) noexcept {
    if (exactBodyBitCount != kUnknownBodyBitCount
        && reader.remaining_bits() != exactBodyBitCount) {
        return false;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::timeSynchronize)) {
        wire::TimeSynchronize probe{};
        if (!wire::read_time_synchronize(reader, probe)) {
            return false;
        }
        answer_time(from, probe);
        return true;
    }
    if (id == wire::kViewMessageId) {
        wire::ViewEstablishment view{};
        if (!wire::read_view(reader, view)) {
            return false;
        }
        bind_view(from, sessionId, view);
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::leaveSession)) {
        std::uint64_t leaving = 0;
        if (!wire::read_session_only(reader, leaving)) {
            return false;
        }
#if defined(RESTORATION_DEDICATED_SERVER)
        // native fast travel leaves the managed region before closing its older PUBLIC BAP
        // connection; resolve the admitted endpoint BEFORE release clears it
        std::uint64_t departingMember=0;
        AcquireSRWLockShared(&g_admittedLock);
        if (const Admitted* record=find_admitted(from,leaving);
            record!=nullptr && record->joinComplete) {
            departingMember=record->activityMemberKey;
        }
        ReleaseSRWLockShared(&g_admittedLock);
        const auto publicSession=departingMember!=0?held_host_session(leaving):0;
        if (publicSession!=0 && state::activity::shared::disconnect_member(publicSession,departingMember)) {
            report(core::log::Level::info,
                "ev=gameplay stage=public_departure result=removed session=0x%016llX activity=0x%016llX",
                static_cast<unsigned long long>(leaving),static_cast<unsigned long long>(publicSession));
        }
#endif
        const bool sent = peer::send_out_of_band(
            from,
            static_cast<std::uint8_t>(wire::SessionMessageId::leaveAcknowledge),
            wire::kLeaveAcknowledgeSize,
            [leaving](bits::Writer& writer) noexcept {
                return wire::write_session_only(writer, leaving);
            });
        report(core::log::Level::info,
               "ev=gameplay stage=leave result=%s session=0x%016llX",
               sent ? "acknowledged" : "fail",
               static_cast<unsigned long long>(leaving));
        release(from, leaving);
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::peerEstablish)) {
        std::uint64_t established = 0;
        if (!wire::read_session_only(reader, established)) {
            return false;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=establish result=ok session=0x%016llX",
               static_cast<unsigned long long>(established));
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::peerReestablish)) {
        std::uint64_t reestablished = 0;
        if (!wire::read_session_only(reader, reestablished)) {
            return false;
        }
        AcquireSRWLockExclusive(&g_admittedLock);
        Admitted* record = nullptr;
        bool migrationAlias = false;
        for (Admitted& candidate : g_admitted) {
            if (!candidate.occupied || candidate.endpoint.address != from.address
                || candidate.endpoint.port != from.port) {
                continue;
            }
            const bool direct = candidate.sessionId == reestablished;
            const bool retainedMigrationAlias =
                candidate.migrationIdentityCaptured
                && candidate.migrationDescriptor.sessionId == candidate.sessionId
                && candidate.migrationDescriptor.machineId == reestablished;
            if (!direct && !retainedMigrationAlias) {
                continue;
            }
            record = &candidate;
            migrationAlias = retainedMigrationAlias;
            break;
        }
        bool queued = false;
        bool migratedActivityHostQueued = false;
        std::uint64_t migratedActivityHost = state::activity::kAbsentSessionId;
        std::uint64_t generation = 0;
        if (record != nullptr) {
            report(core::log::Level::info,
                   "ev=gameplay stage=peer_reestablish result=resolved "
                   "body_session=0x%016llX link_session=0x%016llX "
                   "membership_session=0x%016llX source=%s",
                   static_cast<unsigned long long>(reestablished),
                   static_cast<unsigned long long>(sessionId),
                   static_cast<unsigned long long>(record->sessionId),
                   migrationAlias ? "migration_machine" : "session");
            // the migration alias acknowledges the peer consumed the final kind-22 descriptor; only now is its
            // distinct security id/key safe to install
            if (migrationAlias) {
                dtls::prefer_security_id(from, record->migrationDescriptor.machineId);
                migratedActivityHost = alias_host_session(record->sessionId, reestablished);
                record->migratedSessionId = reestablished;
                record->migratedActivityHostPublished = false;
            }
            ++record->peerReestablishGeneration;
            generation = record->peerReestablishGeneration;
            queued = publish_peer_reestablish_membership(*record);
            if (migrationAlias && migratedActivityHost != state::activity::kAbsentSessionId) {
                migratedActivityHostQueued =
                    publish_activity_host_for_session(*record, reestablished);
                record->migratedActivityHostPublished = migratedActivityHostQueued;
            }
        }
        ReleaseSRWLockExclusive(&g_admittedLock);
        if (migrationAlias) {
            report(migratedActivityHostQueued ? core::log::Level::info : core::log::Level::warn,
                   "ev=gameplay stage=activityhost_alias result=%s source=0x%016llX "
                   "migrated=0x%016llX host=0x%016llX",
                   migratedActivityHostQueued                                  ? "published"
                   : migratedActivityHost == state::activity::kAbsentSessionId ? "missing_source"
                                                                               : "retry_armed",
                   static_cast<unsigned long long>(record != nullptr ? record->sessionId : 0),
                   static_cast<unsigned long long>(reestablished),
                   static_cast<unsigned long long>(migratedActivityHost));
        }
        if (record == nullptr) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=peer_reestablish result=missing_session "
                   "session=0x%016llX link_session=0x%016llX endpoint=0x%08X:%u",
                   static_cast<unsigned long long>(reestablished),
                   static_cast<unsigned long long>(sessionId),
                   from.address,
                   static_cast<unsigned>(from.port));
        } else if (!queued) {
            report(core::log::Level::debug,
                   "ev=gameplay stage=peer_reestablish result=retry_armed "
                   "session=0x%016llX generation=%llu",
                   static_cast<unsigned long long>(reestablished),
                   static_cast<unsigned long long>(generation));
        }
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::hostReestablish)) {
        wire::HostReestablish body{};
        if (!wire::read_host_reestablish(reader, body)) {
            return false;
        }
        bool identityNonzero = false;
        for (const std::byte value : body.identity128) {
            identityNonzero = identityNonzero || value != std::byte{};
        }
        for (const std::byte value : body.identity144) {
            identityNonzero = identityNonzero || value != std::byte{};
        }
        AcquireSRWLockExclusive(&g_admittedLock);
        Admitted* const record = claim(from, body.sessionId);
        const bool retained = record != nullptr && identityNonzero;
        if (retained) {
            record->migrationDescriptor = body;
            record->migrationIdentityCaptured = true;
        }
        ReleaseSRWLockExclusive(&g_admittedLock);
        report(core::log::Level::info,
               "ev=gameplay stage=host_reestablish_capture result=%s session=0x%016llX "
               "machine=0x%016llX identity_nonzero=%u "
               "identity128=0x%016llX,0x%016llX "
               "identity144=0x%016llX,0x%016llX,0x%04llX "
               "address_method=%u",
               retained            ? "retained"
               : record == nullptr ? "missing_session"
                                   : "empty_identity",
               static_cast<unsigned long long>(body.sessionId),
               static_cast<unsigned long long>(body.machineId),
               static_cast<unsigned>(identityNonzero ? 1U : 0U),
               static_cast<unsigned long long>(identity_word(body.identity128, 0)),
               static_cast<unsigned long long>(identity_word(body.identity128, 8)),
               static_cast<unsigned long long>(identity_word(body.identity144, 0)),
               static_cast<unsigned long long>(identity_word(body.identity144, 8)),
               static_cast<unsigned long long>(identity_word(body.identity144, 16, 2)),
               static_cast<unsigned>(body.address[descriptor::kNetAddrSize - 1]));
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinComplete)) {
        wire::JoinComplete body{};
        if (!wire::read_join_complete(reader, body)) {
            return false;
        }
        // join-complete only counts for a row made by the authenticated JoinRequest; an unsolicited message
        // must never allocate an admitted row or host slot. The activity allocator stays outside the lock
        AcquireSRWLockShared(&g_admittedLock);
        const bool admitted = find_admitted(from, body.sessionId) != nullptr;
        ReleaseSRWLockShared(&g_admittedLock);
        if (!admitted) {
            report(core::log::Level::warn,
                   "ev=gameplay stage=join result=unadmitted_complete session=0x%016llX",
                   static_cast<unsigned long long>(body.sessionId));
            return true;
        }

        // the peer repeats this until its membership shows every join member established, so answer with a
        // promoting snapshot. Keyed by the body's session since one link carries every joined region
        AcquireSRWLockExclusive(&g_admittedLock);
        Admitted* const record = find_admitted(from, body.sessionId);
        bool queued = false;
        const bool owed = record != nullptr && !record->joinPublished;
        if (record != nullptr) {
            const bool changed = !record->joinComplete;
            record->joinComplete = true;
            // a pre-establishment player remove is the first half of the platform-row to native-id handoff;
            // hold the promotion until the add arrives so one snapshot carries both established and the player
            if (owed && !record->playerReplacementPending) {
                queued = publish_snapshot(*record);
                record->joinPublished = queued;
            }
            // the peer reads the parameter only once its join is finished and the queue is fullest here, so a
            // refusal is expected and the service slice retries
            if (record->joinPublished && !record->activityHostPublished) {
                record->activityHostPublished = publish_activity_host(*record);
                if (record->activityHostPublished) {
                    record->activityHostPublishedAt = now;
                }
                record->lastRetry = now;
            }
            if (changed) {
                mark_other_members_dirty(record->sessionId, record->endpoint);
            }
        }
        ReleaseSRWLockExclusive(&g_admittedLock);
        report(queued ? core::log::Level::info : core::log::Level::debug,
               "ev=gameplay stage=join result=%s session=0x%llX machine=0x%llX update=%u",
               queued              ? "completed"
               : record == nullptr ? "fail"
               : owed              ? "deferred"
                                   : "repeat",
               static_cast<unsigned long long>(body.sessionId),
               static_cast<unsigned long long>(body.machineId),
               body.joinSequence);
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinAbort)) {
        wire::SessionNotice notice{};
        if (!wire::read_join_abort(reader, notice)) {
            return false;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=join result=abort session=0x%016llX",
               static_cast<unsigned long long>(notice.sessionId));
        release(from, notice.sessionId);
        return true;
    }
    if (id == wire::kParameterRequestId) {
        wire::ParameterRequestHeader header{};
        if (!wire::read_parameter_request(reader, header)) {
            return false;
        }
        const std::uint64_t mask = header.requestedMask & kParameterMaskBits;
        std::array<char, kParameterNameCapacity> names{};
        report(core::log::Level::info,
               "ev=gameplay stage=parameters result=request session=0x%016llX "
               "mask=0x%08X mode=%u names=%s",
               static_cast<unsigned long long>(header.sessionId),
               static_cast<unsigned>(mask),
               static_cast<unsigned>(header.modeFlag ? 1U : 0U),
               wire::parameter_names(mask, names.data(), names.size()));
        const std::uint64_t authorityMask = mask & kActivitySelectionAuthorityMask;
        if (authorityMask != 0) {
            // mode 0 and mode 1 share the body framing; mixed typed/ordinary masks need the ordinary codecs
            // too, so fail closed. Exact 0x20, 0x40 and 0x60 authority subsets are native
            if (header.requestedMask != mask || authorityMask != mask) {
                report(core::log::Level::warn,
                       "ev=gameplay stage=parameter_authority result=header_rejected "
                       "session=0x%016llX raw_mask=0x%016llX mode=%u",
                       static_cast<unsigned long long>(header.sessionId),
                       static_cast<unsigned long long>(header.requestedMask),
                       static_cast<unsigned>(header.modeFlag ? 1U : 0U));
                return false;
            }
            wire::EncodedParameterBody selection{};
            wire::EncodedParameterBody responses{};
            const std::size_t rootsBitCount = reader.remaining_bits();
            std::size_t failedAfterBits = 0;
            const bool selectionRequested = wire::requests(
                authorityMask,
                static_cast<std::uint8_t>(wire::Parameter::activitySelection));
            const bool responsesRequested = wire::requests(
                authorityMask,
                static_cast<std::uint8_t>(wire::Parameter::activitySelectionResponses));
            if (selectionRequested
                && !wire::read_activity_selection_proposal(
                    reader, selection, &failedAfterBits)) {
                report(core::log::Level::warn,
                       "ev=gameplay stage=parameter_authority result=decode_rejected "
                       "root=5 session=0x%016llX mask=0x%08X body_bits=%zu "
                       "roots_bits=%zu failed_after=%zu",
                       static_cast<unsigned long long>(header.sessionId),
                       static_cast<unsigned>(mask),
                       exactBodyBitCount,
                       rootsBitCount,
                       failedAfterBits);
                return false;
            }
            const std::size_t afterSelectionBits = reader.remaining_bits();
            failedAfterBits = 0;
            if (responsesRequested
                && !wire::read_activity_selection_responses_proposal(
                    reader, responses, &failedAfterBits)) {
                report(core::log::Level::warn,
                       "ev=gameplay stage=parameter_authority result=decode_rejected "
                       "root=6 session=0x%016llX mask=0x%08X body_bits=%zu "
                       "roots_bits=%zu selection_bits=%zu failed_after=%zu",
                       static_cast<unsigned long long>(header.sessionId),
                       static_cast<unsigned>(mask),
                       exactBodyBitCount,
                       rootsBitCount,
                       rootsBitCount - afterSelectionBits,
                       failedAfterBits);
                return false;
            }
            if (exactBodyBitCount != kUnknownBodyBitCount) {
                const std::size_t trailing = reader.remaining_bits();
                if (trailing != 0) {
                    report(core::log::Level::warn,
                           "ev=gameplay stage=parameter_authority result=trailing_bits_rejected "
                           "session=0x%016llX trailing=%zu body_bits=%zu",
                           static_cast<unsigned long long>(header.sessionId),
                           trailing,
                           exactBodyBitCount);
                    return false;
                }
            }
            apply_parameter_authority(from,
                                      header.sessionId,
                                      header.modeFlag,
                                      authorityMask,
                                      selection,
                                      responses);
            return true;
        }
        answer_parameters(from, header.sessionId, mask);
        return false;
    }
    if (id == wire::kPeerPropertiesId) {
        wire::PeerPropertiesHeader header{};
        if (!wire::read_peer_properties_header(reader, header)) {
            return false;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=properties result=read session=0x%llX method=%u",
               static_cast<unsigned long long>(header.sessionId),
               static_cast<unsigned>(header.addressMethod));
        return false;
    }
    if (id == wire::kBootMachineId) {
        wire::BootMachineRequest request{};
        if (!wire::read_boot_machine(reader, request) || reader.remaining_bits() != 0) {
            return false;
        }

        state::gameplay::Endpoint targetEndpoint{};
        std::uint64_t targetPlatformId = 0;
        std::uint64_t targetMachineId = 0;
        std::uint64_t lobbyId = 0;
        std::uint64_t actorPlatformId = 0;
        std::size_t matchingTargets = 0;
        bool sourcePresent = false;
        bool sourceSessionExact = false;
        bool sourceOwner = false;

        AcquireSRWLockShared(&g_admittedLock);
        const Admitted* const source = find_admitted(from, request.sessionId);
        sourcePresent = source != nullptr && source->occupied && source->joinComplete
                        && source->joinPublished && source->joiningPlatformId != 0
                        && source->lobbyId != 0;
        // a reliable member message carries its own session id; a zero transport fallback is valid when one
        // endpoint carries both current and TARGET sessions, a nonzero one must still agree
        sourceSessionExact = sourcePresent
                             && (sessionId == 0 || sessionId == request.sessionId);
        sourceOwner = sourceSessionExact
                      && source->joiningPlatformId == source->lobbyOwnerPlatformId;
        if (sourceOwner) {
            lobbyId = source->lobbyId;
            actorPlatformId = source->joiningPlatformId;
            for (const Admitted& candidate : g_admitted) {
                if (!candidate.occupied || &candidate == source
                    || candidate.sessionId != request.sessionId
                    || candidate.lobbyId != source->lobbyId || !candidate.joinComplete
                    || !candidate.joinPublished || candidate.joiningPlatformId == 0
                    || candidate.joiningPlatformId == actorPlatformId) {
                    continue;
                }
                std::array<std::byte, descriptor::kNetAddrSize> address{};
                member_address_for_recipient(candidate, *source, address);
                if (address != request.address) {
                    continue;
                }
                ++matchingTargets;
                targetEndpoint = candidate.endpoint;
                targetPlatformId = candidate.joiningPlatformId;
                targetMachineId = member_machine_id(candidate);
            }
        }
        ReleaseSRWLockShared(&g_admittedLock);

        bool bootQueued = false;
        bool centralRemoved = false;
        bool gameplayRemoved = false;
        if (sourceOwner && matchingTargets == 1 && targetPlatformId != 0
            && targetMachineId != 0) {
#if defined(RESTORATION_DEDICATED_SERVER)
            // commit the lobby mutation before notifying the target; notifying first let the client self-leave
            // in between, the mutation failed and the owner's roster stayed stale
            centralRemoved = ::restoration::realtime_store::remove_lobby_member(
                lobbyId, actorPlatformId, targetPlatformId);
#else
            centralRemoved = true;
#endif
            wire::SessionBoot response{};
            response.sessionId = request.sessionId;
            response.machineId = targetMachineId;
            response.kind = request.kind;
            response.reason = kBootReasonFromUi;
            if (centralRemoved) {
                bootQueued = send_reliable(
                    targetEndpoint,
                    request.sessionId,
                    static_cast<std::uint8_t>(wire::SessionMessageId::sessionBoot),
                    wire::kSessionBootSize,
                    [&response](bits::Writer& writer) noexcept {
                        return wire::write_session_boot(writer, response);
                    });
                release(targetEndpoint, request.sessionId);
                gameplayRemoved = true;
            }
        }

        report(gameplayRemoved ? core::log::Level::info : core::log::Level::warn,
               "ev=gameplay stage=fireteam_boot result=%s session=0x%016llX kind=%u "
               "actor=0x%016llX target=0x%016llX machine=0x%016llX matches=%zu "
               "source_present=%u session_exact=%u source_owner=%u response_queued=%u "
               "central_removed=%u",
               gameplayRemoved && bootQueued ? "removed"
               : gameplayRemoved     ? "removed_notification_deferred"
               : !sourcePresent       ? "source_missing"
               : !sourceSessionExact  ? "session_mismatch"
               : !sourceOwner         ? "not_owner"
               : matchingTargets != 1 ? "target_ambiguous"
               : !centralRemoved      ? "central_rejected"
                                       : "group_rejected",
               static_cast<unsigned long long>(request.sessionId),
               static_cast<unsigned>(request.kind),
               static_cast<unsigned long long>(actorPlatformId),
               static_cast<unsigned long long>(targetPlatformId),
               static_cast<unsigned long long>(targetMachineId),
               matchingTargets,
               static_cast<unsigned>(sourcePresent ? 1U : 0U),
               static_cast<unsigned>(sourceSessionExact ? 1U : 0U),
               static_cast<unsigned>(sourceOwner ? 1U : 0U),
               static_cast<unsigned>(bootQueued ? 1U : 0U),
               static_cast<unsigned>(centralRemoved ? 1U : 0U));
        return true;
    }
    if (id == wire::kPlayerAddId) {
        wire::PlayerAddRequest request{};
        if (!wire::read_player_add(reader, request)) {
            return false;
        }
        const std::uint64_t nativePlatformId = identity_word(request.identity, 0);
        AcquireSRWLockExclusive(&g_admittedLock);
        Admitted* const record = claim(from, request.sessionId);
        bool published = false;
        bool held = false;
        bool identityRejected = false;
        std::uint64_t authenticatedPlatformId = 0;
        std::uint32_t publishedMembershipAdditionNumber = 0;
        if (record != nullptr) {
            authenticatedPlatformId = record->joiningPlatformId;
            identityRejected = !request.identityPresent || authenticatedPlatformId == 0
                               || nativePlatformId != authenticatedPlatformId;
            if (!identityRejected) {
                const bool samePlayer = record->hasPlayer && record->playerId == request.playerId;
                const bool sameIdentity = record->playerIdentityPresent
                                          && record->playerIdentity == request.identity;
                const bool sameRequest = record->playerRequestSequencePresent
                                         && record->playerRequestSequence == request.sequence;
                const bool sameProfile =
                    record->playerProfileSupplement == request.profileSupplement;
                held = samePlayer && sameIdentity && sameRequest && sameProfile
                       && record->playerPublished;
                if (held && record->membershipAdditionNumberPresent) {
                    publishedMembershipAdditionNumber = record->membershipAdditionNumber;
                }
                if (!held) {
                    Admitted candidate = *record;
                    candidate.hasPlayer = true;
                    candidate.playerId = request.playerId;
                    candidate.playerRequestSequence = request.sequence;
                    candidate.playerRequestSequencePresent = true;
                    candidate.playerIdentityPresent = true;
                    candidate.playerIdentity = request.identity;
                    candidate.playerProfileSupplement = request.profileSupplement;
                    candidate.playerReplacementPending = false;
                    candidate.playerPublished = true;
                    PlayerAdditionState* const additions =
                        claim_player_additions(request.sessionId);
                    if (additions != nullptr) {
                        candidate.membershipAdditionNumber =
                            allocate_player_addition(*additions);
                        candidate.membershipAdditionNumberPresent = true;
                        publishedMembershipAdditionNumber =
                            candidate.membershipAdditionNumber;
                        published = publish_snapshot(candidate);
                    }
                    if (published) {
                        record->hasPlayer = true;
                        record->playerId = request.playerId;
                        record->playerRequestSequence = request.sequence;
                        record->playerRequestSequencePresent = true;
                        record->membershipAdditionNumber =
                            candidate.membershipAdditionNumber;
                        record->membershipAdditionNumberPresent = true;
                        record->playerIdentityPresent = true;
                        record->playerIdentity = request.identity;
                        record->playerProfileSupplement = request.profileSupplement;
                        record->playerReplacementPending = false;
                        record->playerPublished = true;
                        if (record->joinComplete) {
                            record->joinPublished = true;
                        }
                        mark_other_members_dirty(record->sessionId, record->endpoint);
                    }
                }
            }
        }
        ReleaseSRWLockExclusive(&g_admittedLock);
        report(identityRejected ? core::log::Level::warn
                                : published ? core::log::Level::info : core::log::Level::debug,
               "ev=gameplay stage=player result=%s session=0x%llX player=0x%llX "
               "request_seq=%u membership_add=%u kind=%u native_identity=%u "
               "native_platform=0x%016llX authenticated_platform=0x%016llX",
               published           ? "added"
               : held              ? "held"
               : record == nullptr ? "missing_session"
               : identityRejected  ? "identity_rejected"
                                   : "deferred",
               static_cast<unsigned long long>(request.sessionId),
               static_cast<unsigned long long>(request.playerId),
               request.sequence,
               publishedMembershipAdditionNumber,
               static_cast<unsigned>(request.kind),
               request.identityPresent ? 1U : 0U,
               static_cast<unsigned long long>(nativePlatformId),
               static_cast<unsigned long long>(authenticatedPlatformId));
        return false;
    }
    if (id == wire::kPlayerRemoveId) {
        wire::PlayerRemoveRequest request{};
        if (!wire::read_player_remove(reader, request)) {
            return false;
        }

        AcquireSRWLockExclusive(&g_admittedLock);
        Admitted* const record = claim(from, request.sessionId);
        bool published = false;
        bool held = false;
        const bool supported = request.playerIndex == kPeerPlayerSlot;
        if (record != nullptr && supported) {
            held = !record->hasPlayer;
            if (!held) {
                // commit the removal only after its snapshot is queued; if the queue is full the unchanged row makes
                // the client's repeated request a safe retry
                Admitted candidate = *record;
                candidate.hasPlayer = false;
                candidate.playerId = 0;
                candidate.playerIdentityPresent = false;
                candidate.playerIdentity = {};
                candidate.playerProfileSupplement = {};
                candidate.membershipAdditionNumberPresent = false;
                candidate.playerPublished = false;
                published = publish_snapshot(candidate);
                if (published) {
                    record->hasPlayer = false;
                    record->playerId = 0;
                    record->playerIdentityPresent = false;
                    record->playerIdentity = {};
                    record->playerProfileSupplement = {};
                    record->membershipAdditionNumberPresent = false;
                    record->playerPublished = false;
                    // every admitted ID36 seen in this build is the remove half of an ID36 -> ID34 identity
                    // replacement; publish it only to its source, endpoint/lobby removal is the real departure path
                    record->playerReplacementPending = true;
                }
            }
        }
        ReleaseSRWLockExclusive(&g_admittedLock);

        report(published ? core::log::Level::info : core::log::Level::debug,
               "ev=gameplay stage=player_remove result=%s session=0x%016llX index=%u",
               published           ? "removed"
               : held              ? "held"
               : record == nullptr ? "missing_session"
               : !supported        ? "unsupported_index"
                                   : "deferred",
               static_cast<unsigned long long>(request.sessionId),
               static_cast<unsigned>(request.playerIndex));
        return true;
    }
    return false;
}

bool publish_membership(const state::gameplay::Endpoint& peer,
                        std::uint64_t peerJoinId,
                        std::uint64_t sessionId,
                        [[maybe_unused]] const middleware::gameplay::peer::JoinMembers* nativeMembers) noexcept {
    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* const record = claim(peer, sessionId);
    bool published = false;
    bool held = false;
    if (record != nullptr) {
        const bool newAttempt = record->joinId != peerJoinId;
        const std::uint64_t priorPlatformId = record->joiningPlatformId;
        if (newAttempt) {
            record->joinId = peerJoinId;
            record->sessionId = sessionId;
            record->joiningPlatformId = priorPlatformId;
            record->activityMemberKey=0;
            record->activityAccount=0;
            record->activitySource=0;
            record->activityCreatedRevision=0;
            record->activityJoinedRevision=0;
            record->activityPlayerId=0;
            record->playerId = 0;
            record->playerRequestSequence = 0;
            record->playerRequestSequencePresent = false;
            record->membershipAdditionNumber = 0;
            record->membershipAdditionNumberPresent = false;
            record->playerIdentityPresent = false;
            record->playerIdentity = {};
            record->playerProfileSupplement = {};
            record->hasPlayer = false;
            record->lobbyId = 0;
            record->lobbyOwnerPlatformId = 0;
            record->initialMembershipPublished = false;
            record->initialParametersPublished = false;
            record->joinComplete = false;
            record->joinPublished = false;
            record->activityHostPublished = false;
            record->activityHostPublishedAt = 0;
            record->initialHostReestablishPublished = false;
            record->peerReestablishGeneration = 0;
            record->peerReestablishMembershipGeneration = 0;
            record->migrationDescriptor = {};
            record->migrationIdentityCaptured = false;
            record->migratedSessionId = 0;
            record->migratedActivityHostPublished = false;
            record->playerPublished = false;
            record->playerReplacementPending = false;
            record->activitySelectionProposal = {};
            record->activitySelectionResponsesProposal = {};
            record->parameterAuthorityDirtyMask = 0;
            record->lastRetry = 0;
        }

        bool identityResolved = record->joiningPlatformId != 0;
        const middleware::gameplay::peer::JoinPlayer* nativePlayer=nullptr;
#if defined(RESTORATION_DEDICATED_SERVER)
        const bool publicRegion =
            source_activity_session(record->sessionId) != state::activity::kAbsentSessionId;
        if (publicRegion) {
            std::uint64_t sourceActivitySessionId = state::activity::kAbsentSessionId;
            std::uint64_t accountSoid = 0;
            const char* bindingStage="source_private";
            identityResolved = resolve_activity_member(
                *record, sourceActivitySessionId, accountSoid,nativeMembers,nativePlayer,bindingStage);
            report(identityResolved ? core::log::Level::info : core::log::Level::warn,
                   "ev=gameplay stage=activity_identity result=%s group=0x%016llX "
                   "source=0x%016llX account=0x%016llX platform=0x%016llX "
                   "native_tables=%u native_peers=%zu native_players=%zu native_selected=%u binding=%s",
                   identityResolved ? "resolved" : "deferred",
                   static_cast<unsigned long long>(record->sessionId),
                   static_cast<unsigned long long>(sourceActivitySessionId),
                   static_cast<unsigned long long>(accountSoid),
                   static_cast<unsigned long long>(record->joiningPlatformId),
                   static_cast<unsigned>(nativeMembers!=nullptr),
                   nativeMembers?nativeMembers->peerCount:0,
                   nativeMembers?nativeMembers->playerCount:0,
                   static_cast<unsigned>(nativePlayer!=nullptr),bindingStage);
        } else {
            identityResolved = identityResolved && record->lobbyId != 0
                               && record->lobbyOwnerPlatformId != 0;
        }
        if (!publicRegion && !identityResolved) {
            middleware::social::realtime::Lobby centralLobby{};
            central_lobby_identity::Result identityResult =
                central_lobby_identity::Result::invalidLobby;
            const std::uint64_t preferredPlatformId =
                record->joiningPlatformId != 0 ? record->joiningPlatformId
                                               : steam::local_steam_id();
            identityResolved = resolve_central_lobby_member(
                *record, preferredPlatformId, centralLobby, identityResult);
            report(identityResolved ? core::log::Level::info : core::log::Level::warn,
                   "ev=gameplay stage=lobby_identity result=%s lobby=0x%016llX "
                   "owner=0x%016llX platform=0x%016llX preferred=0x%016llX members=%zu",
                   central_lobby_identity::name(identityResult),
                   static_cast<unsigned long long>(centralLobby.id),
                   static_cast<unsigned long long>(centralLobby.ownerPlatformId),
                   static_cast<unsigned long long>(record->joiningPlatformId),
                   static_cast<unsigned long long>(preferredPlatformId),
                   centralLobby.memberCount);
        }
#else
        if (!identityResolved) {
            record->joiningPlatformId = steam::local_steam_id();
            identityResolved = record->joiningPlatformId != 0;
        }
#endif
        // the archived successful Citizen transition publishes its player in update one; a player-empty
        // update one followed by the player in update three doesn't retrigger the boundary and ends in Olive
        if (identityResolved && !record->initialMembershipPublished
            && !record->hasPlayer
            && record->playerId == 0 && !record->playerReplacementPending) {
            if (PlayerAdditionState* const additions = claim_player_additions(sessionId);
                additions != nullptr) {
                record->playerId = nativePlayer?nativePlayer->playerId:record->joiningPlatformId;
                if (nativePlayer) {
                    record->playerIdentity=nativePlayer->identity;
                    record->playerIdentityPresent=true;
                }
                record->membershipAdditionNumber = allocate_player_addition(*additions);
                record->membershipAdditionNumberPresent = true;
                record->hasPlayer = true;
            } else {
                identityResolved = false;
            }
        }
        held = record->initialMembershipPublished;
        if (identityResolved && !held) {
            published = publish_snapshot(*record);
            record->initialMembershipPublished = published;
            record->playerPublished = record->hasPlayer && published;
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    if (held) {
        report(core::log::Level::debug,
               "ev=gameplay stage=membership result=held session=0x%016llX join=0x%016llX",
               static_cast<unsigned long long>(sessionId),
               static_cast<unsigned long long>(peerJoinId));
    }
    return published || held;
}

void service(std::uint64_t now) noexcept {
    allocate_claimed_host_sessions();
#if defined(RESTORATION_DEDICATED_SERVER)
    reconcile_central_membership();
#endif
    std::array<std::uint64_t, kAdmittedCapacity> retiredIds{};
    std::array<state::gameplay::Endpoint, kAdmittedCapacity> retiredEndpoints{};
    std::size_t retiredCount = 0;
    AcquireSRWLockExclusive(&g_admittedLock);
    for (Admitted& anchor : g_admitted) {
        if (!anchor.occupied) {
            continue;
        }
        std::size_t held = 0;
        Admitted* oldest = nullptr;
        for (Admitted& candidate : g_admitted) {
            if (!candidate.occupied || candidate.endpoint.address != anchor.endpoint.address
                || candidate.endpoint.port != anchor.endpoint.port) {
                continue;
            }
            ++held;
            if (oldest == nullptr || candidate.lastUse < oldest->lastUse) {
                oldest = &candidate;
            }
        }
        if (held > kPublicSessionCapacity && oldest != nullptr
            && retiredCount < retiredIds.size()) {
            retiredIds[retiredCount] = oldest->sessionId;
            retiredEndpoints[retiredCount] = oldest->endpoint;
            ++retiredCount;
            *oldest = {};
        }
    }
    for (std::size_t index = 0; index < retiredCount; ++index) {
        clear_parameter_authority(retiredIds[index]);
        mark_other_members_dirty(retiredIds[index], retiredEndpoints[index]);
        release_player_additions_if_unused(retiredIds[index]);
    }
    // the PRIVATE activity can publish its Steam address after the first ID30 was acked and an
    // arrival doesn't dirty the roster; without a refresh the movement channel never opens
    for (Admitted& record : g_admitted) {
        if (!record.occupied || source_activity_session(record.sessionId)
                                    == state::activity::kAbsentSessionId) {
            continue;
        }
        std::array<std::byte, descriptor::kNetAddrSize> address{};
        const bool present = public_peer_address(record, address);
        if (present == record.publicPeerConnectionPresent
            && (!present || address == record.publicPeerAddress)) {
            continue;
        }
        record.publicPeerConnectionPresent = present;
        record.publicPeerAddress = address;
        mark_other_members_dirty(record.sessionId, record.endpoint);
        const unsigned method = present ? std::to_integer<unsigned>(address.back()) : 0;
        const bool identityMatches =
            present && public_peer_address_matches_platform(address, record.joiningPlatformId);
        report(present && !identityMatches ? core::log::Level::warn : core::log::Level::info,
               "ev=gameplay stage=public_peer_connection result=%s session=0x%016llX "
               "platform=0x%016llX method=%u address_identity=%s",
               present ? "ready" : "withdrawn",
               static_cast<unsigned long long>(record.sessionId),
               static_cast<unsigned long long>(record.joiningPlatformId),
               method,
               present ? (identityMatches ? "matched" : "mismatch") : "absent");
    }
    for (Admitted& record : g_admitted) {
        const bool activityHostOwed =
            record.occupied && record.joinPublished && !record.activityHostPublished
            && held_host_session(record.sessionId) != state::activity::kAbsentSessionId;
        const bool owed =
            record.occupied
            && ((record.joinComplete && !record.joinPublished && !record.playerReplacementPending)
                || activityHostOwed
                || (record.joinPublished && record.hasPlayer && !record.playerPublished)
                || record.membershipDirty
                || record.parameterAuthorityDirtyMask != 0
                || (record.peerReestablishMembershipGeneration < record.peerReestablishGeneration)
                || (record.migratedSessionId != 0 && !record.migratedActivityHostPublished)
                || (record.activityHostPublished && record.activityHostPublishedAt != 0
                    && !record.initialHostReestablishPublished
                    && now - record.activityHostPublishedAt >= kInitialHostReestablishDelay));
        if (!owed || now - record.lastRetry < kRetryInterval) {
            continue;
        }
        record.lastRetry = now;
        if (!record.joinPublished) {
            record.joinPublished = publish_snapshot(record);
            continue;
        }
        // shared-session membership is authoritative even in plain Orbit with no activity-host session; the
        // optional parameter must never starve a join/leave refresh
        if (record.membershipDirty) {
            if (session_player_replacement_pending(record.sessionId)) {
                continue;
            }
            if (publish_snapshot(record)) {
                record.membershipDirty = false;
            }
            continue;
        }
        if (activityHostOwed) {
            record.activityHostPublished = publish_activity_host(record);
            if (record.activityHostPublished) {
                record.activityHostPublishedAt = now;
            }
            continue;
        }
        if (record.hasPlayer && !record.playerPublished) {
            record.playerPublished = publish_snapshot(record);
            continue;
        }
        if (record.parameterAuthorityDirtyMask != 0) {
            const std::uint64_t pending =
                record.parameterAuthorityDirtyMask & kActivitySelectionAuthorityMask;
            if (publish_parameter_authority_snapshot(record, pending)) {
                record.parameterAuthorityDirtyMask &= ~pending;
            }
            continue;
        }
        if (record.peerReestablishMembershipGeneration < record.peerReestablishGeneration) {
            (void)publish_peer_reestablish_membership(record);
            continue;
        }
        if (record.migratedSessionId != 0 && !record.migratedActivityHostPublished) {
            record.migratedActivityHostPublished =
                publish_activity_host_for_session(record, record.migratedSessionId);
            continue;
        }
        if (!record.initialHostReestablishPublished) {
            record.initialHostReestablishPublished = publish_host_reestablish(record);
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    // outside the lock, in the order release uses; the region's activity host stays because the peer
    // rotates back into a region it hasn't left and a fresh id there is a hard error
    for (std::size_t index = 0; index < retiredCount; ++index) {
        peer::drop_for(retiredEndpoints[index], retiredIds[index]);
        report(core::log::Level::info,
               "ev=gameplay stage=admitted result=retired session=0x%016llX endpoint=%u",
               static_cast<unsigned long long>(retiredIds[index]),
               static_cast<unsigned>(retiredEndpoints[index].port));
    }
}

bool publish_join_parameters(const state::gameplay::Endpoint& peer,
                             std::uint64_t sessionId) noexcept {
    wire::ParameterUpdate update{};
    update.sessionId = sessionId;
    const std::uint64_t goalDataBit =
        std::uint64_t{1}
        << static_cast<std::uint8_t>(wire::Parameter::worldControllerGoalData);
    const std::uint64_t currentActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::currentActivity);
    const std::uint64_t previousActivityBit =
        std::uint64_t{1} << static_cast<std::uint8_t>(wire::Parameter::previousActivity);
    const bool publicRegion =
        source_activity_session(sessionId) != state::activity::kAbsentSessionId;
    if (publicRegion) {
        // a Citizen join belongs to the selected destination, not Orbit: publish the private
        // source's current activity too, or without parameter 7 the controller tears down (Olive)
        update.releasedMask = goalDataBit;
        update.carriedMask = currentActivityBit | previousActivityBit;
    } else {
        update.carriedMask = goalDataBit | currentActivityBit;
    }
    if (!fill_current_activity(update.currentActivity, sessionId)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=parameters result=deferred public=%u "
               "reason=current_activity source=0x%016llX group=0x%016llX",
               static_cast<unsigned>(publicRegion ? 1U : 0U),
               static_cast<unsigned long long>(source_activity_session(sessionId)),
               static_cast<unsigned long long>(sessionId));
        return false;
    }
    if (publicRegion) {
        fill_previous_activity(update.previousActivity);
    }

    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* record = nullptr;
    for (Admitted& candidate : g_admitted) {
        if (candidate.occupied && candidate.sessionId == sessionId
            && candidate.endpoint.address == peer.address && candidate.endpoint.port == peer.port) {
            record = &candidate;
            break;
        }
    }
    const bool held = record != nullptr && record->initialParametersPublished;
    const bool sent = record != nullptr && !held
                      && send_reliable(peer,
                                       sessionId,
                                       wire::kParameterUpdateId,
                                       wire::kParameterUpdateSize,
                                       [&update](bits::Writer& writer) {
                                           return wire::write_parameter_update(writer, update);
                                       });
    if (record != nullptr && sent) {
        record->initialParametersPublished = true;
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    std::array<char, kParameterNameCapacity> names{};
    report(sent   ? core::log::Level::info
           : held ? core::log::Level::debug
                  : core::log::Level::warn,
           "ev=gameplay stage=parameters result=%s public=%u carried=0x%08X released=0x%08X "
           "names=%s",
           sent   ? "queued"
           : held ? "held"
                  : "fail",
           static_cast<unsigned>(publicRegion ? 1U : 0U),
           static_cast<unsigned>(update.carriedMask),
           static_cast<unsigned>(update.releasedMask),
           wire::parameter_names(
               update.carriedMask | update.releasedMask, names.data(), names.size()));
    return sent || held;
}

bool view_accepted(const state::gameplay::Endpoint& endpoint, std::uint64_t sessionId) noexcept {
    return peer::view_bound_for(endpoint, sessionId);
}

bool authorizes_player_publication(const state::gameplay::Endpoint& endpoint,
                                   std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_admittedLock);
    bool authorized = false;
    for (const Admitted& record : g_admitted) {
        if (record.occupied && record.sessionId == sessionId
            && record.endpoint.address == endpoint.address
            && record.endpoint.port == endpoint.port && record.joiningPlatformId != 0
            && record.hasPlayer && record.playerId != 0 && record.playerPublished) {
            authorized = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_admittedLock);
    return authorized;
}

bool ready_for_player_properties_replay(const state::gameplay::Endpoint& endpoint,
                                        std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_admittedLock);
    bool ready = false;
    for (const Admitted& record : g_admitted) {
        if (record.occupied && record.sessionId == sessionId
            && record.endpoint.address == endpoint.address
            && record.endpoint.port == endpoint.port && record.joiningPlatformId != 0
            && record.hasPlayer && record.playerId != 0 && record.playerPublished
            && !record.membershipDirty) {
            ready = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_admittedLock);
    return ready;
}

bool native_activity_binding(const state::gameplay::Endpoint& endpoint,
    std::uint64_t groupSessionId, NativeActivityBinding& output, bool includePublishedJoining) noexcept {
    output = {};
    AcquireSRWLockShared(&g_admittedLock);
    const auto* record = find_admitted(endpoint, groupSessionId);
    const bool valid = record && (record->joinComplete || includePublishedJoining) && record->initialMembershipPublished
        && record->activityAccount && record->activitySource && record->activityMemberKey
        && record->activityCreatedRevision && record->activityJoinedRevision;
    if (valid) output = {record->activityAccount, record->activitySource, record->activityMemberKey,
        record->activityCreatedRevision, record->activityJoinedRevision, record->joinId};
    ReleaseSRWLockShared(&g_admittedLock);
    return valid;
}

void snapshot_admitted(std::span<AdmittedRow> output, std::size_t& count) noexcept {
    count = 0;
    AcquireSRWLockShared(&g_admittedLock);
    for (const Admitted& entry : g_admitted) {
        if (!entry.occupied || count >= output.size()) {
            continue;
        }
        output[count] = {entry.sessionId,
                         entry.endpoint,
                         entry.joinComplete,
                         entry.activityHostPublished,
                         entry.playerPublished};
        ++count;
    }
    ReleaseSRWLockShared(&g_admittedLock);
}

void reset() noexcept {
    g_membershipProjectionDumpCount.store(0);
    reset_host_sessions();
    AcquireSRWLockExclusive(&g_admittedLock);
    g_admitted = {};
    g_playerAdditions = {};
    ReleaseSRWLockExclusive(&g_admittedLock);
}

} // namespace dawn::server::gameplay::group
