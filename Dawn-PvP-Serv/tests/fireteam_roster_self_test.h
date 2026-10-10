#pragma once

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "../restorationserver/src/account_store.h"
#include "../restorationserver/src/realtime_store.h"
#include "core/identity/device_identity.h"
#include "middleware/bap/account_translation/account_translation_response.h"
#include "middleware/bap/account_translation/request_observation.h"
#include "middleware/encoding/byte_order.h"
#include "state/equipment/light/resolution/configured_equipment_light_resolver.h"

namespace restoration::tests {
inline bool fireteam_roster_self_test(void* module, const dawn::state::AccountState& seed) noexcept {
    namespace state = dawn::state;
    namespace wire = dawn::middleware::social::realtime;
    namespace tr = dawn::middleware::bap::account_translation;
    namespace enc = dawn::middleware::encoding;
    namespace desc = dawn::middleware::gameplay::descriptor;
    constexpr std::array<std::uint64_t,4> ids{0x0110000100F00101ULL,
        0x0110000100F00102ULL,0x0110000100F00103ULL,0x0110000100F00104ULL};
    const auto check = [](bool ok, const char* label) noexcept {
        if (!ok) std::printf("FAIL: fireteam roster: %s\n", label);
        return ok;
    };
    struct Cleanup { ~Cleanup() { realtime_store::shutdown(); account_store::shutdown(); } } cleanup;
    account_store::LoadResult owner{};
    if (!check(account_store::initialize(module, seed, "Roster-Host", ids[0], owner), "database"))
        return false;
    std::array<std::uint64_t,4> accounts{owner.account.primarySoid};
    std::array<std::array<char,account_store::kAccountNameCapacity>,4> names{};
    names[0] = {'R','o','s','t','e','r','-','H','o','s','t'};
    for (std::size_t i=1; i<ids.size(); ++i) {
        std::array<std::byte,dawn::core::identity::device_identity::kPublicKeySize> key{};
        key.fill(static_cast<std::byte>(i));
        account_store::DeviceBinding binding{};
        if (!check(account_store::load_or_create_device(ids[i],ids[i],key,binding),"fixture enrollment"))
            return false;
        accounts[i] = binding.accountSoid;
        state::AccountState profile{}; std::uint64_t rev{};
        if (!check(account_store::profile_by_platform(ids[i],profile,rev,names[i]),"fixture profile"))
            return false;
    }
    std::array<char,512> message{}; std::size_t written{};
    for (std::size_t i=1; i<=2; ++i) {
        if (!check(account_store::execute_relationship(ids[0],account_store::RelationshipAction::add,
                names[i].data(),100,message,written)
            && account_store::execute_relationship(ids[i],account_store::RelationshipAction::accept,
                names[0].data(),100,message,written),"host friends both guests")) return false;
    }
    if (!check(account_store::execute_relationship(ids[1],account_store::RelationshipAction::add,
            names[3].data(),100,message,written),"pending stranger request")) return false;
    for (std::size_t i=0; i<ids.size(); ++i) {
        if (!check(realtime_store::begin_game_session(ids[i],accounts[i],1,100)
            && realtime_store::mark_gameplay_ready(ids[i],accounts[i],1,100),"ready sessions")) return false;
    }
    wire::Request req{}; static wire::Response reply{};
    const auto run = [&](std::size_t i) noexcept {
        return realtime_store::execute(ids[i],accounts[i],100,req,&account_store::accepted_friend,reply)
            && reply.result==1;
    };
    req.operation=wire::Operation::createLobby; req.valueA=3; req.valueB=6;
    if (!check(run(0),"create host fireteam")) return false;
    const auto lobby=reply.lobby.id;
    desc::JoinEndpoint endpoint{};endpoint.machineId=ids[0];endpoint.address=0xCB00712A;
    endpoint.port=27016;endpoint.onlineSessionId=lobby;
    realtime_store::GameplayBootstrapDescriptor descriptor{};
    if (!check(desc::build(endpoint,descriptor),"descriptor")) return false;
    req={};req.operation=wire::Operation::publishNativeFireteamDescriptor;req.lobbyId=lobby;
    req.payloadSize=descriptor.size();std::copy(descriptor.begin(),descriptor.end(),req.payload.begin());
    if (!check(run(0),"publish host fireteam")) return false;
    state::AccountState profile{};std::uint64_t soid{},revision{};
    if (!check(!account_store::resolve_friend_account_callback(nullptr,ids[1],ids[2],soid)
        && !account_store::resolve_friend_profile_callback(nullptr,accounts[1],accounts[2],profile),
        "unrelated guests denied before joining")) return false;
    req={};req.operation=wire::Operation::joinLobby;req.lobbyId=lobby;
    if (!check(run(1) && run(2),"both guests join their friend")) return false;
    for (const auto pair : {std::array<std::size_t,2>{1,2},std::array<std::size_t,2>{2,1}}) {
        const auto a=pair[0],b=pair[1];
        if (!check(!account_store::accepted_friend(ids[a],ids[b])
            && realtime_store::shares_fireteam(ids[a],ids[b])
            && realtime_store::shares_fireteam_accounts(accounts[a],accounts[b])
            && account_store::resolve_friend_account_callback(nullptr,ids[a],ids[b],soid)
            && soid==accounts[b]
            && account_store::resolve_friend_profile_callback(nullptr,accounts[a],accounts[b],profile)
            && profile.primarySoid==accounts[b] && profile.characterCount!=0,
            "nonfriend guest resolves the correct full roster profile")) return false;
        state::AccountState expected{};std::array<char,account_store::kAccountNameCapacity> name{};
        if (!check(account_store::profile_by_platform(ids[b],expected,revision,name),"actual profile")) return false;
        bool equipped = false;
        for (std::size_t slot=0;slot<profile.characters[0].equipment.slots.size();++slot) {
            const auto& got=profile.characters[0].equipment.slots[slot];
            const auto& want=expected.characters[0].equipment.slots[slot];
            if (!check(got.has_value()==want.has_value()
                && (!got || (got->definitionHash==want->definitionHash && got->level==want->level)),
                "roster returns the actual equipped power inputs")) return false;
            equipped = equipped || (got && got->level>0);
        }
        if (!check(equipped && account_store::friend_profile(ids[a],ids[b],profile,revision)
            && profile.primarySoid==accounts[b],"equipped profile and HTTP access")) return false;
    }
    if (!check(!account_store::resolve_friend_account_callback(nullptr,ids[1],ids[3],soid)
        && !account_store::friend_profile(ids[1],ids[3],profile,revision),"pending request is not access")) return false;
    struct Context { std::uint64_t viewer, own; } context{ids[1],accounts[1]};
    const auto resolver=+[](void* ptr,std::uint64_t id) noexcept {
        auto& c=*static_cast<Context*>(ptr);std::uint64_t account{};
        if (id==c.viewer) return c.own;
        (void)account_store::resolve_friend_account_callback(nullptr,c.viewer,id,account);return account;
    };
    std::array<std::byte,36> request{};enc::write_u16_be(std::span(request).first<2>(),4);
    request[2]=std::byte{12};request[3]=std::byte{255};
    const std::array<std::uint64_t,4> order{ids[3],ids[2],ids[1],ids[0]};
    for (std::size_t i=0;i<order.size();++i) enc::write_u64_be(std::span(request).subspan(4+i*8).first<8>(),order[i]);
    std::array<std::byte,64> answer{};
    if (!check(tr::encode_response(request,&context,resolver,answer,written) && written==52
        && enc::read_u16_be(std::span(answer).first<2>())==3,"batch replies all authorized rows")) return false;
    const std::array<std::size_t,3> expected{2,1,0};
    for (std::size_t i=0;i<expected.size();++i)
        if (!check(enc::read_u64_be(std::span(answer).subspan(4+i*8).first<8>())==ids[expected[i]]
            && enc::read_u64_be(std::span(answer).subspan(4+expected.size()*8+i*8).first<8>())==accounts[expected[i]],
            "native batch source array then account array")) return false;
    answer.fill(std::byte{0xAA});const auto untouched=answer;
    if (!check(!tr::encode_response(request,&context,resolver,std::span(answer).first(51),written)
        && written==0 && answer==untouched,"small output is atomic")) return false;
    for (std::size_t n=0;n<request.size();++n)
        if (!check(tr::encode_response(std::span(request).first(n),&context,resolver,answer,written)
            && written==4 && enc::read_u16_be(std::span(answer).first<2>())==0,"truncated batches neutral")) return false;
    if (!check(!tr::request_matches_platform(request,ids[3]),"scalar helper rejects batches")) return false;
    req={};req.operation=wire::Operation::createLobby;req.valueA=3;req.valueB=6;
    if (!check(run(3),"stranger has own lobby")) return false;
    realtime_store::Presence presence{};
    (void)realtime_store::presence(ids[1],1000,presence);
    if (!check(realtime_store::shares_fireteam(ids[1],ids[2])
        && !realtime_store::shares_fireteam(ids[1],ids[3]),"retained party and lobby isolation")) return false;
    req={};req.operation=wire::Operation::leaveLobby;req.lobbyId=lobby;
    if (!check(run(2) && !account_store::resolve_friend_account_callback(nullptr,ids[1],ids[2],soid)
        && !account_store::resolve_friend_profile_callback(nullptr,accounts[1],accounts[2],profile)
        && !account_store::friend_profile(ids[1],ids[2],profile,revision)
        && account_store::resolve_friend_account_callback(nullptr,ids[0],ids[2],soid),
        "leave revokes party access while accepted friendship survives")) return false;
    req.operation=wire::Operation::joinLobby;
    if (!check(run(2) && realtime_store::begin_game_session(ids[2],accounts[2],2,1001)
        && !realtime_store::shares_fireteam_accounts(accounts[1],accounts[2]),"reconnect revokes old membership")) return false;
    if (!check(!account_store::accepted_friend(ids[1],ids[2]),"no friendship was fabricated")) return false;
    std::puts("PASS: real account-store fireteam roster, mutual nonfriend profile/equipment, batched lookup, isolation and revocation");
    return true;
}
} // namespace restoration::tests
