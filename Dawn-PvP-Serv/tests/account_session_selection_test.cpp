#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "core/settings/settings.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"
#include "state/session/session_runtime.h"

namespace dawn::core::settings {
Settings fixture{};
const Settings& get() noexcept { return fixture; }
}
namespace dawn::state::runtime::storage {
SRWLOCK g_stateLock{SRWLOCK_INIT};
AccountState fixture{};
AccountState& account() noexcept { return fixture; }
}
namespace {
std::atomic<unsigned> calls{};
bool fail{};
bool initialize(void*, std::uint64_t accountSoid) noexcept {
    ++calls;
    return !fail && dawn::state::begin_character_selection(accountSoid);
}
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
}
int main() {
    namespace session=dawn::state::session;
    auto& account=dawn::state::runtime::storage::fixture;
    account.primarySoid=0x9EAA000100200100ULL;
    account.characterCount=3;
    for (std::size_t i=0;i<3;++i) {
        account.characters[i].soid=account.primarySoid+i+1;
        account.characters[i].nextInventorySerial=100+static_cast<std::uint32_t>(i);
    }
    account.characters[1].lastOrbitedDestination=0x13A023BC;
    account.characters[1].equipment.slots[1].emplace();
    account.characters[1].equipment.slots[1]->instanceSoid=0x4DBE450737F57D21ULL;
    account.characters[1].equipment.slots[1]->definitionHash=1798874854;
    account.characters[1].equipment.slots[1]->mutationSerial=99;
    account.characters[1].inventory.count=1;
    account.characters[1].inventory.values[0].instanceSoid=0x4000000000001234ULL;
    account.characters[1].inventory.values[0].definitionHash=0x12345678;
    account.characters[1].selected=true;
    static dawn::state::AccountState expected{};expected=account;
    for(auto& character:expected.characters)character.selected=false;
    dawn::state::SignOnState bootstrap{};bootstrap.bootstrapTokenPresent=true;
    session::Credentials first{},second{},other{};
    require(session::issue(account.primarySoid,1001,bootstrap,100,first),"real session issued");
    require(!dawn::state::begin_character_selection(account.primarySoid+1),"wrong scoped owner refused");
    require(account.characters[1].selected,"wrong owner preserves selection");
    auto tampered=first;++tampered.accountSoid;
    require(!session::initialize_game_generation(tampered,100,initialize,nullptr),"forged account refused");
    tampered=first;tampered.signOn.sessionToken[0]^=std::byte{1};
    require(!session::initialize_game_generation(tampered,100,initialize,nullptr),"forged token refused");
    require(calls==0,"unverified credentials never invoke initializer");
    fail=true;
    require(!session::initialize_game_generation(first,100,initialize,nullptr),"failed reset refused");
    require(account.characters[1].selected,"failed reset preserves selection");
    fail=false;
    require(session::initialize_game_generation(first,100,initialize,nullptr),"failed reset retry succeeds");
    require(std::memcmp(&account,&expected,sizeof account)==0,"only selection changed; equipment and identity preserved");
    account.characters[2].selected=true;
    require(session::initialize_game_generation(first,101,initialize,nullptr),"additional hello accepted");
    require(account.characters[2].selected&&calls==2,"additional hello preserves committed pick");
    require(session::issue(account.primarySoid,1001,bootstrap,102,second),"game-only relog new generation issued");
    require(!session::initialize_game_generation(first,102,initialize,nullptr),"revoked generation refused");
    require(account.characters[2].selected,"revoked credentials cannot clear pick");
    calls=0;
    std::atomic<bool> passed{true};std::vector<std::thread> threads;
    for(int i=0;i<8;++i)threads.emplace_back([&] {
        if(!session::initialize_game_generation(second,102,initialize,nullptr))passed=false;
    });
    for(auto& thread:threads)thread.join();
    require(passed&&calls==1,"concurrent connection hellos initialize exactly once");
    require(std::memcmp(&account,&expected,sizeof account)==0,"fresh generation clears cached selection only");
    account.characters[0].selected=true;
    require(!session::initialize_game_generation(second,second.expiresAt,initialize,nullptr),"expired generation refused");
    require(account.characters[0].selected,"expired generation cannot mutate account");
    require(session::issue(account.primarySoid+100,2002,bootstrap,103,other),"another identity issued");
    require(!session::initialize_game_generation(other,103,initialize,nullptr),"wrong runtime binding cannot initialize");
    require(account.characters[0].selected,"another account cannot clear this account");
    session::shutdown();
    require(!session::initialize_game_generation(second,104,initialize,nullptr),"shutdown revokes initializer");
    std::puts("PASS: authenticated generation selection reset, repeat hello, retry, relog and concurrency");
}
