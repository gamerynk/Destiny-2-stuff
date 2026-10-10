#pragma once
#include <chrono>
#include <cstring>
#include <vector>
#include "../restorationserver/src/account_store.h"
#include "../restorationserver/src/account_http.h"
#include "core/identity/device_identity.h"
#include "server/http/server_http.h"

namespace restoration::tests {
inline bool account_name_self_test(void* module, const dawn::state::AccountState& seed) noexcept {
    namespace id = dawn::core::identity::device_identity;
    namespace http = dawn::server::http;
    constexpr std::uint64_t actor=0x0110000100F00901ULL, other=0x0110000100F00902ULL;
    const auto check=[](bool ok,const char* label) {
        if (!ok) std::printf("FAIL: account name: %s\n",label);
        return ok;
    };
    struct Cleanup { ~Cleanup() {
        http::unregister_extension_consumer(&account_http::consume);
        id::shutdown(); account_store::shutdown();
    }} cleanup;
    account_store::LoadResult owner{};
    if (!check(account_store::initialize(module,seed,"Name-Owner",actor+10,owner)
        && id::initialize(module,actor),"fixture initialize")) return false;
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::array<std::byte,id::kProofSize> proof{},wrongScope{},expired{};
    id::Claims claims{};
    account_store::DeviceBinding self{},peer{};
    std::array<std::byte,id::kPublicKeySize> peerKey{}; peerKey.fill(std::byte{2});
    if (!check(id::issue(id::Scope::accountRename,now,proof)
        && id::verify(id::Scope::accountRename,proof,now,claims)
        && account_store::load_or_create_device(claims.deviceId,actor,claims.publicKey,self)
        && account_store::load_or_create_device(other,other,peerKey,peer)
        && id::issue(id::Scope::socialRelationships,now,wrongScope)
        && id::issue(id::Scope::accountRename,now-3600,expired)
        && http::register_extension_consumer(&account_http::consume),"authenticated fixtures")) return false;
    std::array<char,256> message{}; std::size_t written{};
    std::array<char,account_store::kAccountNameCapacity> beforeName{},afterName{},peerName{};
    static dawn::state::AccountState before{},after{},otherProfile{};
    std::uint64_t beforeRevision{},afterRevision{},peerRevision{};
    if (!check(account_store::profile_by_platform(actor,before,beforeRevision,beforeName)
        && account_store::rename_account(other,"Taken-Name",now,message,written)
        && account_store::execute_relationship(actor,account_store::RelationshipAction::add,
            "Taken-Name",now,message,written)
        && account_store::execute_relationship(other,account_store::RelationshipAction::accept,
            beforeName.data(),now,message,written),"existing account and friendship")) return false;
    auto request=[&](std::span<const std::byte> auth,std::string_view name,
                     int declared=-1,bool extra=false) {
        std::vector<std::byte> body(auth.begin(),auth.end());
        body.push_back(static_cast<std::byte>(declared<0?name.size():declared));
        for (char ch:name) body.push_back(static_cast<std::byte>(ch));
        if(extra) body.push_back(std::byte{1});
        std::array<std::byte,512> output{};
        dawn::client::network::HttpRequest req{dawn::client::network::HttpMethod::post,
            "/v1/account/name","application/octet-stream",{},body,output};
        dawn::client::network::HttpResponse reply{};
        return http::consume_public(req,reply)?reply.statusCode:0U;
    };
    auto forged=proof; forged.back()^=std::byte{1};
    if (!check(request({},"NoAuth")==401 && request(wrongScope,"WrongScope")==401
        && request(forged,"Forged")==401 && request(expired,"Expired")==401,
        "reject missing, wrong-scope, forged and expired proofs")) return false;
    if (!check(request(proof,"abc")==200 && request(proof,std::string(63,'x'))==200
        && request(proof,"ExamplePlayer")==200
        && request(proof,"taken-name")==400
        && request(proof,"NoTrailing",10,true)==400
        && request(proof,"ab")==400 && request(proof,std::string(64,'x'))==400
        && request(proof," invalid")==400 && request(proof,"invalid ")==400
        && request(proof,"bad;name")==400 && request(proof,std::string_view("bad\0name",8))==400
        && request(proof,"bad\nname")==400,
        "rename; reject duplicate, length, framing, spaces and control characters")) return false;
    if (!check(account_store::profile_by_platform(actor,after,afterRevision,afterName)
        && std::strcmp(afterName.data(),"ExamplePlayer")==0
        && after.primarySoid==before.primarySoid && afterRevision==beforeRevision
        && account_store::profile_by_platform(other,otherProfile,peerRevision,peerName)
        && std::strcmp(peerName.data(),"Taken-Name")==0
        && account_store::accepted_friend(actor,other),
        "only caller renamed; identity, revision and friendship preserved")) return false;
    std::array<std::byte,256> output{};
    dawn::client::network::HttpResponse response{};
    dawn::client::network::HttpRequest req{dawn::client::network::HttpMethod::get,
        "/v1/account/name",{},{},{},output};
    if (!check(!http::consume_public(req,response),"GET not public")) return false;
    req.method=dawn::client::network::HttpMethod::post;req.url="/v1/account/name/other";
    if (!check(!http::consume_public(req,response),"other account path not public")) return false;
    account_store::shutdown();
    if (!check(account_store::initialize(module,seed,"Name-Owner",actor+10,owner)
        && account_store::profile_by_platform(actor,after,afterRevision,afterName)
        && std::strcmp(afterName.data(),"ExamplePlayer")==0,"rename survives database reopen")) return false;
    std::puts("PASS: account name authentication, persistence, uniqueness and ownership");
    return true;
}
}
