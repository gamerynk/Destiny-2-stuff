#include <array>
#include <cstdio>
#include "../dawn/src/state/account/inventory/weapon_roll_policy.h"

int main() {
    namespace policy = dawn::state::account::inventory::weapon_roll;
    namespace details = policy::details;
    namespace sockets = policy::sockets;
    std::array<details::Definition, 4> definitions{};
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        auto& definition = definitions[i];
        definition.definitionIndex = static_cast<std::uint16_t>(i);
        definition.equipmentSlot = std::int8_t{7};
        definition.instancedDefinitionState = details::InstancedDefinitionState::instanced;
        definition.ordinarySocketCount = 2;
        definition.socketTypes[0] = 10;
        definition.socketTypes[1] = 20;
        definition.initialPlugIndices[0] = static_cast<std::uint16_t>(100 + i);
    }
    definitions[2].equipmentSlot = std::int8_t{1};
    definitions[3].socketTypes[0] = details::kUnavailableSocketType;
    const std::array<sockets::Rule, 5> rules{{{0,0,0,1},{1,0,0,2},{1,1,0,3},{2,0,0,4},{3,0,0,5}}};
    const std::array<sockets::Pool, 6> pools{{{0,0},{0,1},{1,1},{2,1},{3,1},{4,1}}};
    const std::array<sockets::Member, 5> members{{200,201,202,203,204}};
    policy::Catalog catalog;
    if (!catalog.build(definitions, rules, pools, members)
        || !catalog.allowed(0,0,100) || !catalog.allowed(0,0,101)
        || !catalog.allowed(0,0,201) || !catalog.allowed(0,1,202)
        || catalog.allowed(0,0,202) || catalog.allowed(0,0,203)
        || catalog.allowed(0,0,102) || catalog.allowed(0,0,204)
        || catalog.allowed(3,0,201) || !catalog.allowed(3,0,204)
        || catalog.allowed(2,0,203) || catalog.allowed(0,12,200)) return 1;
    auto bad = pools; bad[1].memberCount = 99;
    if (catalog.build(definitions, rules, bad, members) || catalog.allowed(0,0,100)) return 2;
    std::puts("PASS: same-socket custom weapon rolls; wrong lane, armor, unknown lanes and malformed pools rejected");
    return 0;
}
