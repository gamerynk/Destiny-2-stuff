#include <Windows.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "client/hooking/detour.h"
#include "client/hooks/external_server/route.h"
#include "client/hooks/network/content_config/internal.h"
#include "client/hooks/network/content_config/runtime.h"
#include "client/patterns/image_scan.h"
#include "core/settings/settings.h"
namespace {
using Setopt=int(*)(void*,int,void*) noexcept;
dawn::core::settings::Settings configured{};
Setopt replacement{};
bool detachFailure{}, installFailure{}, enterAtInstall{};
std::atomic_bool blockNative{},nativeEntered{},releaseNative{true};
std::atomic_uint originalCalls{},detachCalls{};
int lastOption{};void* lastValue{};std::string lastUrl;
std::thread installEntrant;
int original(void*,int option,void* value) noexcept {
    ++originalCalls;lastOption=option;lastValue=value;
    if(option==10002)lastUrl=static_cast<char*>(value);
    if(blockNative) {nativeEntered=true;while(!releaseNative.load())std::this_thread::yield();}
    return 17;
}
}
namespace dawn::core::settings {const Settings& get() noexcept{return configured;}}
namespace dawn::core::log {void write(Channel,Level,std::string_view) noexcept{}}
namespace dawn::client::hooks::network::content_config::native_work {
bool restore_when_idle(const Metadata&,gate::Ownership& ownership) noexcept{return gate::restore(ownership);}
}
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>,const char*) noexcept {
    static std::byte code{};return &code;
}
}
namespace dawn::client::hooking::detour {
bool install(const Spec& spec,Handle& handle) noexcept {
    if(installFailure)return false;
    replacement=reinterpret_cast<Setopt>(spec.replacement);handle={reinterpret_cast<void*>(&original),spec.replacement,true};
    if(enterAtInstall)installEntrant=std::thread([]{assert(replacement(nullptr,64,reinterpret_cast<void*>(1))==17);});
    return true;
}
UninstallResult uninstall(Handle& handle,std::span<const ProtectedCodeEntry> entries,IdleCheck idleCheck) noexcept {
    ++detachCalls;assert(entries.size()==3&&idleCheck());
    if(detachFailure)return UninstallResult::failed;
    handle={};return UninstallResult::removed;
}
}
int main() {
    namespace route=dawn::client::hooks::external_server;
    namespace content=dawn::client::hooks::network::content_config;
    configured.client.externalServer.enabled=false;assert(!route::install()&&!route::has_ownership());
    configured.client.externalServer.enabled=true;
    strcpy_s(configured.client.externalServer.origin.data(),configured.client.externalServer.origin.size(),"https://configured.example:443");
    std::byte gate{1};assert(content::gate::disable(&gate,content::g_gateOwnership));content::g_accepting=true;
    installFailure=true;assert(!route::install()&&!route::has_ownership());installFailure=false;
    enterAtInstall=true;assert(route::install());installEntrant.join();enterAtInstall=false;
    assert(route::is_installed()&&route::has_ownership());
    for(const auto option:{64,81,20011,123456}) {
        void* const value=reinterpret_cast<void*>(static_cast<std::uintptr_t>(option));
        assert(replacement(nullptr,option,value)==17&&lastOption==option&&lastValue==value);
    }
    gate=std::byte{1};char url[]="https://retail.example/SignOn?opaque=kept";
    assert(replacement(nullptr,10002,url)==17&&gate==std::byte{0});
    assert(lastUrl=="https://configured.example:443/SignOn?opaque=kept");
    for(const auto emptyPath:{"HTTPS://retail.example?opaque=kept#tag","HtTp://retail.example#tag"}) {
        assert(replacement(nullptr,10002,const_cast<char*>(emptyPath))==17);
        assert(lastUrl==(std::strchr(emptyPath,'?')
            ? "https://configured.example:443/?opaque=kept#tag"
            : "https://configured.example:443/#tag"));
    }
    auto before=originalCalls.load();
    for(const auto malformed:{"file://local/config","https://","https:///path","https://old/path\r\nattack"}) {
        assert(replacement(nullptr,10002,const_cast<char*>(malformed))==3);
    }
    std::array<char,513> oversized{};oversized.fill('x');oversized.back()='\0';
    assert(replacement(nullptr,10002,oversized.data())==3&&replacement(nullptr,10002,nullptr)==3);
    configured.client.externalServer.origin[0]='\0';assert(replacement(nullptr,10002,url)==3);
    assert(originalCalls==before);
    strcpy_s(configured.client.externalServer.origin.data(),configured.client.externalServer.origin.size(),"http://127.0.0.1:8080");
    content::g_accepting=false;assert(replacement(nullptr,10002,url)==2&&originalCalls==before);content::g_accepting=true;
    detachFailure=true;assert(!route::uninstall()&&route::has_ownership()&&route::is_installed());
    assert(replacement(nullptr,10002,url)==17);detachFailure=false;
    blockNative=true;releaseNative=false;nativeEntered=false;
    std::thread active([&]{assert(replacement(nullptr,10002,url)==17);});
    while(!nativeEntered.load())std::this_thread::yield();
    std::atomic_bool detachStarted{},detached{};
    const auto detachBefore=detachCalls.load();
    std::thread remover([&]{detachStarted=true;detached=route::uninstall();});
    while(!detachStarted.load())std::this_thread::yield();
    assert(detachCalls==detachBefore);releaseNative=true;active.join();remover.join();
    assert(detached&&!route::has_ownership()&&!route::is_installed());
    blockNative=false;assert(content::uninstall()&&gate==std::byte{1});
    assert(!content::prepare_native_request());
    std::puts("PASS: native option/TLS pass-through, URL fail-closed routing, publish-before-admission, failed-detach ownership, active-call drain and gate restore");
}
