#include "mission_observations.h"

#include <Windows.h>

#include "../strike_pact/runtime.h"
#include "mission_host.h"

namespace dawn::state::activity::mission_observations {
namespace {

namespace wire = middleware::mission::observation_wire;
namespace strike = strike_pact;

[[nodiscard]] strike::EnemyReceipt receipt(const wire::Enemy& enemy) noexcept {
    return {enemy.run, enemy.actor, enemy.owner, enemy.generation, enemy.source, enemy.registry};
}

[[nodiscard]] wire::Enemy enemy(const strike::EnemyReceipt& receipt) noexcept {
    return {receipt.run, receipt.actor, receipt.owner, receipt.generation, receipt.source,
            receipt.registry};
}

void apply_one(const wire::Receipt& item, bool authority) noexcept {
    switch (item.kind) {
    case wire::Kind::admission:
        if (authority) {
            static_cast<void>(strike::observe_admission(receipt(item.enemy)));
        }
        break;
    case wire::Kind::death:
        if (authority) {
            static_cast<void>(strike::observe_death(receipt(item.enemy)));
        }
        break;
    case wire::Kind::readiness:
        if (authority) {
            coo::EnemyReadiness value{};
            value.created = item.readiness.created;
            value.health = item.readiness.health;
            value.ai = item.readiness.ai;
            value.tactical = item.readiness.tactical;
            value.healthHandle = item.readiness.healthHandle;
            value.tacticalRegistry = item.readiness.tacticalRegistry;
            value.tacticalSlot = item.readiness.tacticalSlot;
            value.tacticalRow = item.readiness.tacticalRow;
            strike::observe_readiness(receipt(item.enemy), value);
        }
        break;
    case wire::Kind::capacity:
        if (authority) {
            strike::observe_capacity(item.capacity.run,
                                     {item.capacity.known, item.capacity.used,
                                      item.capacity.maximum});
        }
        break;
    case wire::Kind::health:
        if (authority) {
            static_cast<void>(strike::observe_health(receipt(item.enemy), item.healthFraction));
        }
        break;
    case wire::Kind::submission:
        strike::observe_submission(item.submission.run, item.submission.definition,
                                   item.submission.offset, item.submission.bank,
                                   item.submission.row, item.submission.generation);
        break;
    }
}

} // namespace

Result apply(std::uint64_t platformId, const wire::Request& request,
             wire::Response& response) noexcept {
    response = {};
    if (!mission_host::member(platformId)) {
        return Result::notMember;
    }
    const bool authority = mission_host::authoritative(platformId);
    for (std::size_t index = 0; index < request.count; ++index) {
        apply_one(request.receipts[index], authority);
    }
    response.run = strike::native_run();
    if (response.run == 0) {
        return Result::applied;
    }
    const strike::BossRequest boss = strike::boss_request();
    response.boss.ownerRun = boss.owner.run;
    response.boss.ownerValue = boss.owner.value;
    response.boss.enemy = enemy(boss.enemy);
    response.boss.stage = boss.stage;
    response.boss.revision = boss.revision;
    response.boss.requested = boss.requested;
    if (authority) {
        const auto readiness = strike::readiness_request(GetTickCount64());
        for (std::size_t index = 0;
             index < readiness.count && response.readinessCount < response.readiness.size();
             ++index) {
            response.readiness[response.readinessCount++] = enemy(readiness.actors[index]);
        }
    }
    return Result::applied;
}

} // namespace dawn::state::activity::mission_observations
