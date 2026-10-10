#include "gameplay_advertisement.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../state/activity/definition.h"
#include "../../state/activity/runtime.h"
#include "../../state/activity/shared/public_admission.h"
#include "../../state/activity/shared/social_admission.h"
#include "../../state/runtime/runtime.h"
#include "endpoint/gameplay_endpoint.h"
#include "gameplay_log.h"
#include "group/group_host.h"
#include "group/group_host_sessions.h"

namespace dawn::server::gameplay {

namespace {

namespace message = middleware::bap::activity_message::replicate_membership;

// member-slot fields are 6 bits at bias 1, so the slot can't exceed 62
constexpr std::uint8_t kMaximumMemberSlot = 62;
constexpr std::uint8_t kQueriedMemberSlot = 0;
constexpr std::uint64_t kRegionStride = 0x9E3779B97F4A7C15ULL;

// derived rather than allocated so every push for a region names the same value with no table
[[nodiscard]] std::uint64_t region_identity(std::uint64_t base, std::int32_t regionIndex) noexcept {
    const auto region = static_cast<std::uint64_t>(static_cast<std::uint32_t>(regionIndex));
    const std::uint64_t derived = base ^ (kRegionStride * (region + 1U));
    return derived == 0 ? kRegionStride : derived;
}

[[nodiscard]] std::uint64_t mix_identity(std::uint64_t value) noexcept {
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

constexpr std::uint64_t kNoOutcome = ~0ULL;
constexpr unsigned kSlotShift = 32;
constexpr unsigned kReasonShift = 40;
constexpr unsigned kSourceShift = 48;

enum class Skip : std::uint64_t {
    none,
    notReady,
    noRegion,
    slotRange,
    descriptor,
    noHostSession,
    hostSessionFull
};

std::atomic<std::uint64_t> g_reported{kNoOutcome};

constexpr const char* kSources[] = {"reported", "arrival"};

void report_outcome(Skip skip,
                    std::int32_t regionIndex,
                    RegionSource regionSource,
                    std::uint8_t ambassadorSlot) noexcept {
    const auto outcome = static_cast<std::uint64_t>(static_cast<std::uint32_t>(regionIndex))
                         | (static_cast<std::uint64_t>(ambassadorSlot) << kSlotShift)
                         | (static_cast<std::uint64_t>(skip) << kReasonShift)
                         | (static_cast<std::uint64_t>(regionSource) << kSourceShift);
    if (g_reported.exchange(outcome, std::memory_order_relaxed) == outcome) {
        return;
    }
    const char* source = kSources[static_cast<std::size_t>(regionSource)];
    if (skip == Skip::none) {
        report(core::log::Level::info,
               "ev=gameplay stage=advertise result=ok region=%d region_source=%s "
               "ambassador_slot=%u",
               static_cast<int>(regionIndex),
               source,
               static_cast<unsigned>(ambassadorSlot));
        return;
    }
    static constexpr const char* kReasons[] = {"none",
                                               "not_ready",
                                               "no_region",
                                               "slot_range",
                                               "descriptor",
                                               "no_host_session",
                                               "host_session_full"};
    report(core::log::Level::info,
           "ev=gameplay stage=advertise result=skip reason=%s region=%d region_source=%s",
           kReasons[static_cast<std::size_t>(skip)],
           static_cast<int>(regionIndex),
           source);
}

// an equal slot sends the peer into the local-ambassador stage and it never reaches the citizen join
[[nodiscard]] std::uint8_t ambassador_slot(std::uint8_t localMemberSlot) noexcept {
    return localMemberSlot == 0 ? 1 : 0;
}

[[nodiscard]] Skip build_candidate(std::uint64_t sourceSessionId,
                                   std::int32_t regionIndex,
                                   std::uint8_t localMemberSlot,
                                   message::CitizenAdvertisement& candidate) noexcept {
    candidate = {};
    // the Tower's authored regions are PRV and its primary activity is shared by social matchmaking,
    // so advertising an unused citizen host would not share it
    if(state::activity::shared::social_session(sourceSessionId)) return Skip::noRegion;
    if (!endpoint::ready()) {
        return Skip::notReady;
    }
    if (regionIndex < 0) {
        return Skip::noRegion;
    }
    if (localMemberSlot > kMaximumMemberSlot) {
        return Skip::slotRange;
    }
    const endpoint::Identity identity = endpoint::identity();
    const state::gameplay::Endpoint advertised = endpoint::advertised();
    middleware::gameplay::descriptor::JoinEndpoint join{};
    join.address = advertised.address;
    join.port = advertised.port;
    // the client keys managed sessions by the descriptor's machine id, not the session id, so regions
    // in separate PRIVATE matches need distinct identities
    endpoint::Identity regionIdentity{};
    if (!region_descriptor_identity(sourceSessionId, regionIndex, identity, regionIdentity)) {
        return Skip::noHostSession;
    }
    join.machineId = regionIdentity.machineId;
    join.onlineSessionId = regionIdentity.onlineSessionId;
    // the region compares this with the activity-host parameter and errors with
    // public_activity_host_mismatch if they differ; the key is the machine id, which the peer echoes
    bool claimedSlot = false;
    const std::uint64_t hostSession =
        group::activity_host_session(
            join.machineId, regionIndex, sourceSessionId, claimedSlot);
    if (hostSession == state::activity::kAbsentSessionId) {
        // a claimed slot gets filled by the next service slice, an unclaimed one never will; callers must tell them apart
        return claimedSlot ? Skip::noHostSession : Skip::hostSessionFull;
    }
    if (!middleware::gameplay::descriptor::build(join, candidate.descriptor)) {
        candidate = {};
        return Skip::descriptor;
    }
    candidate.memberKey = join.machineId;
    middleware::gameplay::descriptor::write_net_addr(
        join.address, join.port, candidate.address);
    candidate.onlineSessionId = hostSession;
    candidate.regionIndex = regionIndex;
    candidate.ambassadorSlot = ambassador_slot(localMemberSlot);
    candidate.present = true;
    return Skip::none;
}

} // namespace

bool region_descriptor_identity(std::uint64_t sourceSessionId,
                                std::int32_t regionIndex,
                                const endpoint::Identity& base,
                                endpoint::Identity& output) noexcept {
    output = {};
    if (sourceSessionId == 0 || regionIndex < 0 || base.machineId == 0 || base.onlineSessionId == 0)
        return false;

    std::uint64_t accountSoid{};
    auto context = state::capture_account_runtime(accountSoid);
    state::activity::SessionRecord source{};
    if (!state::activity::snapshot_for_runtime(context, accountSoid, sourceSessionId, source)
        || source.restoredDormant) return false;
    if (state::activity::shared::compatible_public_destination(source.destination, source.destination)) {
        output = {region_identity(base.machineId, regionIndex),
                  region_identity(base.onlineSessionId, regionIndex)};
        return true;
    }

    // a managed host's keepalive names the allocated host, the initial advertisement names its PRIVATE
    // source; both must resolve to the same match incarnation
    const auto privateSource = group::source_activity_session_for_host(sourceSessionId);
    if (privateSource != state::activity::kAbsentSessionId) sourceSessionId = privateSource;
    state::AccountRuntimeContext canonical{};
    if (state::activity::shared::canonical_context(sourceSessionId, canonical)) {
        context = canonical;
        accountSoid = context.account->primarySoid;
    }
    if (!state::activity::snapshot_for_runtime(context, accountSoid, sourceSessionId, source)
        || source.restoredDormant) return false;

    auto scoped = base;
    if (!state::activity::shared::compatible_public_destination(source.destination, source.destination)) {
        // numeric session ids can repeat across account runtimes; canonical ownership makes teammates
        // agree and the source incarnation keeps repeated matches apart
        const auto scope = mix_identity(accountSoid)
            ^ mix_identity(source.sessionId ^ 0xD1B54A32D192ED03ULL)
            ^ mix_identity(source.createdRevision ^ 0x8CB92BA72F3D8DD7ULL);
        scoped.machineId = mix_identity(base.machineId ^ scope ^ 0xDB4F0B9175AE2165ULL);
        scoped.onlineSessionId = mix_identity(base.onlineSessionId ^ scope ^ 0xBBE0563303A4615FULL);
    }
    output = {region_identity(scoped.machineId, regionIndex),
              region_identity(scoped.onlineSessionId, regionIndex)};
    return true;
}

void build_advertisement(std::uint64_t sourceSessionId,
                         std::int32_t regionIndex,
                         RegionSource regionSource,
                         std::uint8_t localMemberSlot,
                         message::CitizenAdvertisement& output) noexcept {
    const Skip skip = build_candidate(sourceSessionId, regionIndex, localMemberSlot, output);
    report_outcome(skip, regionIndex, regionSource, output.ambassadorSlot);
}

AdvertisementState advertisement_state(std::uint64_t sourceSessionId,
                                       std::int32_t regionIndex) noexcept {
    message::CitizenAdvertisement candidate{};
    switch (build_candidate(sourceSessionId, regionIndex, kQueriedMemberSlot, candidate)) {
    case Skip::none:
        return AdvertisementState::ready;
    case Skip::noHostSession:
        return AdvertisementState::pending;
    default:
        return AdvertisementState::absent;
    }
}

} // namespace dawn::server::gameplay
