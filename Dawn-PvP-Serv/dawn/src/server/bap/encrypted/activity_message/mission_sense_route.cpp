#include "mission_sense_route.h"

#include <array>
#include <cstdio>
#include <memory>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/activity_message/incident.h"
#include "../../../../middleware/bap/activity_message/sense_update.h"
#include "../../../../state/activity/coo/native_player_trigger.h"
#include "../../../../state/activity/mission_host/mission_host.h"
#include "../../../../state/activity/strike_pact/runtime.h"

namespace dawn::server::bap::encrypted::activity_message::mission_sense {
namespace {

namespace service = middleware::bap::activity_message;
namespace strike = state::activity::strike_pact;

[[nodiscard]] bool bound_to_mission(std::uint64_t boundSessionId,
                                    const service::Request& request) noexcept {
    return boundSessionId != 0 && request.accountHandle == boundSessionId
           && boundSessionId == state::activity::mission_host::session();
}

// the decoded update holds up to 512 objects, far too big for a worker stack; one per thread, reused
[[nodiscard]] service::sense_update::SenseUpdate* scratch_update() noexcept {
    thread_local std::unique_ptr<service::sense_update::SenseUpdate> update;
    if (!update) {
        update.reset(new (std::nothrow) service::sense_update::SenseUpdate{});
    }
    return update.get();
}

void report_refusal(std::size_t payloadBytes, std::size_t consumedBits) noexcept {
    static std::size_t lastBytes{};
    if (lastBytes == payloadBytes) {
        return;
    }
    lastBytes = payloadBytes;
    std::array<char, 192> line{};
    const int written = std::snprintf(line.data(), line.size(),
                                      "ev=mission_sense stage=parse result=refused bytes=%zu "
                                      "consumed_bits=%zu",
                                      payloadBytes, consumedBits);
    if (written > 0) {
        core::log::write(core::log::Channel::server, core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

void observe_sense_update(std::uint64_t boundSessionId, const service::Request& request) noexcept {
    if (!bound_to_mission(boundSessionId, request)) {
        return;
    }
    const std::uint64_t run = strike::native_run();
    if (run == 0) {
        return;
    }
    service::sense_update::SenseUpdate* update = scratch_update();
    if (update == nullptr) {
        return;
    }
    *update = {};
    std::size_t consumedBits = 0;
    if (!service::sense_update::parse_sense_update(request.payload, *update, consumedBits)) {
        report_refusal(request.payload.size(), consumedBits);
        return;
    }
    for (std::size_t index = 0; index < update->objectCount; ++index) {
        const service::sense_update::SenseObject& object = update->objects[index];
        if (object.hasSquadOutput) {
            strike::TaskCosts costs{};
            costs.mask = object.squadOutput.costMask;
            costs.revision = object.squadOutput.revision;
            costs.hasRevision = object.squadOutput.hasRevision;
            costs.initialized = object.squadOutput.initialized;
            for (std::size_t group = 0; group < costs.cost.size(); ++group) {
                costs.cost[group] = object.squadOutput.cost[group];
            }
            strike::observe_costs(object.registryKey, object.slotIndex, costs);
        }
        if (object.slotType == 37 && object.hasForestGeneratorState) {
            strike::observe_generator(run, object.registryKey, object.slotIndex, object.forestSeed,
                                      object.forestActive32);
        }
        if (object.slotType == 43 && object.hasSceneOutput) {
            strike::observe_scene(run, object.registryKey, object.slotIndex, object.sceneOutput);
        }
        if (object.slotType == 1 && object.hasSquadOutput) {
            strike::observe_squad(run, object.registryKey, object.slotIndex, object.squadOutput);
        }
        if (object.slotType == 2 && object.hasCombatantOutput) {
            strike::observe_combatant(run, object.registryKey, object.slotIndex,
                                      object.combatantOutput);
        }
        if (object.slotType == 30 && object.hasMonitorOutput) {
            const auto& monitor = object.monitorOutput;
            strike::observe_monitor(object.registryKey, object.slotIndex, monitor.any, monitor.count,
                                    monitor.value);
        }
    }
}

void observe_incident(std::uint64_t boundSessionId, const service::Request& request) noexcept {
    namespace incident = service::incident;
    namespace player_trigger = state::activity::coo::native_player_trigger;
    if (!bound_to_mission(boundSessionId, request)) {
        return;
    }
    incident::Incident parsed;
    if (incident::validate(request.payload, parsed) != incident::Verdict::accepted
        || !parsed.hasPayload || parsed.primaryTarget != player_trigger::kIncident) {
        return;
    }
    const std::uint64_t run = strike::native_run();
    player_trigger::Receipt receipt{};
    if (run != 0
        && player_trigger::decode(std::span(parsed.payload).first(parsed.payloadLength), receipt)) {
        strike::observe_player_trigger(run, receipt.registry,
                                       static_cast<std::uint16_t>(receipt.slot));
    }
}

} // namespace dawn::server::bap::encrypted::activity_message::mission_sense
