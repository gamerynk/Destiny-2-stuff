#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include "client/hooks/network/runtime.h"
#include "core/logging/log.h"
#include "client/hooks/network/lifecycle/network_hook_entries.h"
#include "client/hooks/network/coordinator/network_call_coordinator.h"

namespace {
struct Resource { bool owned{}, ready{}, failInstall{}, failRemove{}; } gate, route, investment;
std::string events;
bool platformAttached{}, platformAccepting{}, busy{}, selfActive{}, failPlatformRemove{};
bool nativeCertificate{};
bool nativeAuthentication{}, adaptiveSelected{}, adaptiveHealthy{}, failPlatformInstall{};
std::size_t attachedCount{};
void reset() { gate={}; route={}; investment={}; events.clear(); platformAttached=false;
    platformAccepting=false; busy=false; selfActive=false; failPlatformRemove=false; adaptiveHealthy=false; failPlatformInstall=false; }
bool install(Resource& r, char event) { events+=event; if (r.failInstall) return false;
    r.owned=r.ready=true; return true; }
bool releaseResource(Resource& r, char event) { events+=event; if (r.failRemove) return false;
    r.owned=r.ready=false; return true; }
}
namespace dawn::client::hooks::network {
SRWLOCK g_lock{SRWLOCK_INIT};
namespace platform {
bool native_certificate_requested() noexcept { return nativeCertificate; }
bool native_authentication_requested() noexcept { return nativeAuthentication && nativeCertificate; }
bool automatic_native_selected() noexcept { return adaptiveSelected; }
void select_automatic_native(bool value) noexcept { adaptiveSelected=value; }
bool automatic_native_ready() noexcept { assert(!selfActive); return adaptiveHealthy; }
}
namespace content_config {
bool install() noexcept { return ::install(gate,'G'); }
bool uninstall() noexcept { assert(!route.owned); return ::releaseResource(gate,'g'); }
bool is_installed() noexcept { return gate.ready; }
bool has_ownership() noexcept { return gate.owned; }
}
namespace investment {
bool install() noexcept { assert(gate.ready && route.ready); return ::install(::investment,'I'); }
bool uninstall() noexcept { return ::releaseResource(::investment,'i'); }
bool is_installed() noexcept { return ::investment.ready; }
bool has_ownership() noexcept { return ::investment.owned; }
}
namespace coordinator {
bool current_thread_active() noexcept { return selfActive; }
bool wait_for_idle(DWORD) noexcept { events+='W'; return !busy; }
LONG active_calls() noexcept { return busy ? 1 : 0; }
}
namespace lifecycle {
bool all_installed(std::span<const HookSlot>) noexcept { return platformAttached; }
bool any_installed(std::span<const HookSlot>) noexcept { return platformAttached; }
bool all_accepting(std::span<const HookSlot>) noexcept { return platformAccepting; }
void set_accepting(std::span<const HookSlot>, bool value) noexcept { platformAccepting=value; }
bool install_group(std::span<const hooking::detour::Spec> specs,std::span<const HookSlot> slots) noexcept {
    assert(specs.size() == slots.size()); attachedCount = slots.size();
    events+='P'; if(failPlatformInstall) return false; platformAttached=platformAccepting=true; return true; }
bool uninstall_group(std::span<const HookSlot>,std::span<const hooking::detour::ProtectedCodeEntry>) noexcept {
    events+='p'; if(failPlatformRemove) return false; platformAttached=false; return true; }
PlatformSpecs platform_specs() noexcept { return {}; }
PlatformProtectedEntries platform_protected_entries() noexcept { return {}; }
}
}
namespace dawn::client::hooks::external_server {
bool install() noexcept { assert(gate.ready); return ::install(route,'R'); }
bool uninstall() noexcept { return ::releaseResource(route,'r'); }
bool is_installed() noexcept { return route.ready; }
bool has_ownership() noexcept { return route.owned; }
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept {}
}
int main() {
    using namespace dawn::client::hooks::network;
    reset(); assert(install_game()); assert(events=="GRI"); assert(is_game_installed());
    assert(!is_installed()); assert(has_game_ownership()); assert(install_game()); assert(events=="GRI");
    assert(install_platform()); assert(is_installed()); assert(uninstall());
    assert(events=="GRIPWprig"); assert(!is_game_installed()&&!has_game_ownership());
    assert(attachedCount == 2);
    reset(); nativeCertificate=true; assert(install_game()); assert(install_platform());
    assert(attachedCount == 1 && is_platform_installed() && is_installed());
    assert(uninstall()); assert(!is_platform_installed()); nativeCertificate=false;
    reset(); nativeCertificate=nativeAuthentication=true;
    assert(install_game()); assert(install_platform()); assert(events=="GRI");
    assert(!platformAttached && is_platform_installed() && is_installed());
    assert(uninstall()); nativeCertificate=nativeAuthentication=false;
    reset(); gate.failInstall=true; assert(!install_game()); assert(events=="Gir"); assert(!route.owned);
    reset(); route.failInstall=true; assert(!install_game()); assert(events=="GRirg"); assert(!gate.owned);
    reset(); ::investment.failInstall=true; assert(!install_game()); assert(events=="GRIirg");
    assert(!has_game_ownership());
    reset(); ::investment.failInstall=true; route.failRemove=true; assert(!install_game());
    assert(events=="GRIir"); assert(route.owned&&gate.owned&&has_game_ownership());
    route.failRemove=false; assert(uninstall()); assert(!has_game_ownership());
    reset(); assert(install_game()); route.failRemove=true; events.clear(); assert(!uninstall());
    assert(events=="Wpr"); assert(gate.owned&&route.owned&&::investment.owned);
    route.failRemove=false; assert(uninstall()); assert(!has_game_ownership());
    reset(); assert(install_game()); ::investment.failRemove=true; events.clear(); assert(!uninstall());
    assert(events=="Wpri"); assert(gate.owned&&!route.owned&&::investment.owned);
    ::investment.failRemove=false; assert(uninstall()); assert(!has_game_ownership());
    reset(); assert(install_game()); gate.failRemove=true; assert(!uninstall()); assert(has_game_ownership());
    gate.failRemove=false; assert(uninstall());
    reset(); assert(install_game()); assert(install_platform()); busy=true; events.clear();
    assert(!uninstall()); assert(events=="W"&&gate.owned&&route.owned&&platformAttached);
    busy=false; assert(uninstall());
    reset(); assert(install_game()); selfActive=true; events.clear(); assert(!uninstall()); assert(events.empty());
    selfActive=false; assert(uninstall());
    reset(); assert(install_game()); assert(install_platform()); failPlatformRemove=true; events.clear();
    assert(!uninstall()); assert(events=="Wp"&&gate.owned&&route.owned);
    failPlatformRemove=false; assert(uninstall());
    for(auto* r : {&gate,&route,&::investment}) { reset(); r->owned=true;
        assert(!install_game()); assert(events.empty()&&has_game_ownership()); }
    reset(); assert(install_game()); assert(install_platform()); adaptiveHealthy=true; events.clear();
    assert(service_platform_policy()); assert(events=="Wp" && adaptiveSelected && !platformAttached);
    assert(is_platform_installed()); assert(service_platform_policy()); assert(events=="Wp");
    adaptiveHealthy=false; events.clear(); failPlatformInstall=true;
    assert(!service_platform_policy()); assert(events=="P" && adaptiveSelected && !platformAttached);
    failPlatformInstall=false; assert(service_platform_policy());
    assert(!adaptiveSelected && platformAttached && platformAccepting && attachedCount==2);
    adaptiveHealthy=true; failPlatformRemove=true; events.clear();
    assert(service_platform_policy()); assert(events=="Wp" && !adaptiveSelected && platformAttached && platformAccepting);
    failPlatformRemove=false; busy=true; events.clear();
    assert(service_platform_policy()); assert(events=="W" && !adaptiveSelected && platformAttached && platformAccepting);
    busy=false; selfActive=true; events.clear(); assert(service_platform_policy()); assert(events.empty());
    selfActive=false; assert(service_platform_policy()); assert(adaptiveSelected && !platformAttached);
    assert(uninstall()); assert(!adaptiveSelected); assert(service_platform_policy());
    assert(!platformAttached);
    std::puts("PASS: required native curl startup, dependency order, rollback retention, quiescence and retry");
}

