#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "../dawn/src/client/hooks/bootflow/remote_facing_control.h"
namespace f=dawn::client::hooks::bootflow::remote_facing_control;
namespace d=dawn::client::hooks::bootflow::remote_facing_diagnostic;
namespace p=dawn::client::hooks::bootflow::remote_facing_policy;
namespace hook=dawn::client::hooking::detour;
static unsigned checks{},applyCalls{},passCalls{},directionCalls{},identityCalls{};
static bool detachFailure{},reenter{},logReentry{};
static std::vector<std::string> logs;
static void require(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL:%s\n",label);std::exit(1);}}
template<class T> static void put(void* root,std::size_t offset,const T& value){std::memcpy(static_cast<std::byte*>(root)+offset,&value,sizeof(value));}
struct Fixture {
    alignas(16) std::array<std::byte,0x8000> rows{};
    std::array<std::byte,0x80> table{};
    std::array<std::byte,8> header{};
    std::array<std::byte,1024> suppression{};
    std::byte* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x2700000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    std::byte* guard=static_cast<std::byte*>(VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    std::byte* unit=rows.data()+0x2000;
    std::byte* other=rows.data()+0x4000;
    Fixture(){
        if(!image||!guard)return;
        put(image,0x2439C70,header.data());put(header.data(),0,table.data());put(table.data(),8,rows.data());put(table.data(),0x30,std::uint32_t{0x2000});
        put(image,0x1FBA9C8,suppression.data());
        init(unit,1,3);init(other,2,4);
        auto* source=guard+0x1000-0x30;
        put(source,0,std::uint32_t{0x80000003});put(source,4,std::uint32_t{0x80801234});
        put(source,0x24,std::uint32_t{0x2001});put(source,0x2C,std::uint32_t{7});
        put(table.data(),0x48,source-0x1000);put(table.data(),0x70,std::uint32_t{0x1000});
        DWORD old{};VirtualProtect(guard+0x1000,0x1000,PAGE_NOACCESS,&old);
        put(unit,0x97C,std::uint32_t{0x2001});put(unit,0x984,std::uint32_t{UINT32_MAX});
        put(unit,0xA5C,std::uint32_t{0x2001});put(unit,0xA60,std::uint32_t{UINT32_MAX});
        put(unit,0x980,std::int8_t{0});put(unit,0x988,std::int8_t{-1});put(unit,0x98C,UINT32_MAX);
        put(unit,0x3F0,std::uint8_t{3});put(unit,0x3F4,std::int32_t{55});
    }
    static void init(void* unit,std::uint32_t owner,std::uint32_t legacy){
        put(unit,0,std::uint32_t{0x80C0C51A});put(unit,4,std::uint32_t{0x80803A00});
        put(unit,0x24,owner);put(unit,0x2C,legacy);put(unit,0xACE,std::uint8_t{1});put(unit,0x410,std::uint8_t{2});put(unit,0x8A0,UINT32_MAX);
        const p::Vector prior{0,1,0,0};put(unit,0x420,prior);put(unit,0xD0,prior);put(unit,0xE0,prior);
        const std::array<float,3> xyz{0,1,0};put(unit,0xB14,xyz);put(unit,0xB38,xyz);
    }
    ~Fixture(){if(image)VirtualFree(image,0,MEM_RELEASE);if(guard)VirtualFree(guard,0,MEM_RELEASE);}
    std::uintptr_t base()const{return reinterpret_cast<std::uintptr_t>(image);}
};
static Fixture* fixture{};
static void __fastcall native_apply(void* unit,const void*,void*) noexcept {
    ++applyCalls;
    if(reenter){reenter=false;f::apply(unit,nullptr,nullptr);}
    put(unit,0x420,p::Vector{1,0,0,0});SetLastError(123);
}
static void __fastcall native_pass() noexcept {++passCalls;SetLastError(456);}
static void __fastcall native_direction(void* unit,void*) noexcept {
    ++directionCalls;put(unit,0xD0,p::Vector{1,0,0,0});put(unit,0xE0,p::Vector{1,0,0,0});SetLastError(789);
}
static bool counted_identity(const void* unit,p::Key& key,p::State& state) noexcept {++identityCalls;return f::identify(unit,key,state);}
namespace dawn::core::log {
void write(Channel,Level,std::string_view message) noexcept {
    logs.emplace_back(message);
    if(logReentry){logReentry=false;d::observe(fixture->base(),fixture->unit,"log_reentry",&counted_identity,true);}
    SetLastError(999);
}}
namespace dawn::client::hooking::detour {
UninstallResult uninstall(std::span<Handle> handles,std::span<const ProtectedCodeEntry> entries,IdleCheck idle) noexcept {
    require(handles.size()==2&&entries.size()==2,"both existing facing hooks retained");
    if(!idle())return UninstallResult::protectedCodeActive;
    if(detachFailure)return UninstallResult::failed;
    for(auto& h:handles)h={};return UninstallResult::removed;
}}
#if RESTORATION_REMOTE_FACING_TRACE
static HANDLE entered{},resume{};
static bool held_identity(const void* unit,p::Key& key,p::State& state) noexcept {
    const auto ok=f::identify(unit,key,state);SetEvent(entered);WaitForSingleObject(resume,INFINITE);return ok;
}
static bool changed_identity(const void* unit,p::Key& key,p::State& state) noexcept {
    ++identityCalls;const auto ok=f::identify(unit,key,state);
    if(identityCalls==1)put(fixture->unit,0x24,std::uint32_t{9});return ok;
}
#endif
int main(){
    Fixture state;fixture=&state;require(state.image&&state.guard,"fixture allocations");
    f::imageBase=state.base();f::originalApply=&native_apply;f::originalPass=&native_pass;f::nativeDirection=&native_direction;
    f::handles[0]={reinterpret_cast<void*>(&native_apply),reinterpret_cast<void*>(&f::apply),true};
    f::handles[1]={reinterpret_cast<void*>(&native_pass),reinterpret_cast<void*>(&f::control_pass),true};
    d::start();SetLastError(11);f::apply(state.unit,nullptr,nullptr);
    require(applyCalls==1&&GetLastError()==123,"apply original and LastError preserved");
    f::control_pass();require(passCalls==1&&directionCalls==1&&GetLastError()==456,"control original and workaround preserved");
    const std::array<float,3> expected{1,0,0};std::array<float,3> facing{};std::memcpy(facing.data(),state.unit+0xB14,sizeof(facing));
    require(facing==expected,"existing facing repair unchanged");
#if RESTORATION_REMOTE_FACING_TRACE
    require(logs.size()==6,"five diagnostic stages plus existing publication log");
    require(logs[0].find("stage=apply_pre_original")!=std::string::npos&&logs[0].find("received=0,1,0")!=std::string::npos,"pre-apply native state captured");
    require(logs[1].find("stage=apply_post_original_pre_queue")!=std::string::npos&&logs[1].find("received=1,0,0")!=std::string::npos,"post-native apply precedes DLL queue");
    require(logs[3].find("stage=control_post_original_pre_repair")!=std::string::npos&&logs[3].find("facing=0,1,0")!=std::string::npos,"native control state sampled before repair");
    require(logs.back().find("stage=control_post_repair")!=std::string::npos&&logs.back().find("facing=1,0,0")!=std::string::npos,"repair explicitly separate from native result");
    require(logs[0].find("future_packet=unqualified")!=std::string::npos&&logs[0].find("native_clock=unobserved")!=std::string::npos,"no invented future packet clock");
    require(logs[0].find("root0=1:")!=std::string::npos&&logs[0].find("0x80801234")!=std::string::npos,"declared source exact owner/type logged without unit-offset access");
    require(logs[0].find("dep_count=4294967295 dep_rows=unobserved")!=std::string::npos,"unqualified dependency count does not trigger array reads");
    logs.clear();f::apply(state.other,nullptr,nullptr);require(logs.empty(),"one selected owner only");
    auto s=d::source_identity(state.base(),0x2001);require(s.identity&&s.owner==0x2001,"source fresh salted identity");
    put(state.guard+0x1000-0x30,0x24,std::uint32_t{0x2002});s=d::source_identity(state.base(),0x2001);
    require(!s.identity,"recycled source row refuses stale owner");put(state.guard+0x1000-0x30,0x24,std::uint32_t{0x2001});
    require(!d::source_identity(state.base(),UINT32_MAX).identity&&!d::source_identity(state.base(),0).identity,"invalid source handles refused");
    d::stop();d::start();identityCalls=0;logs.clear();d::observe(state.base(),state.unit,"identity_changed",&changed_identity,true);
    require(logs.empty(),"changed identity between native reads refuses sample");put(state.unit,0x24,std::uint32_t{1});
    identityCalls=0;logReentry=true;d::observe(state.base(),state.unit,"log_reentry_outer",&counted_identity,true);
    require(logs.size()==1&&identityCalls==2,"diagnostic logging reentry skips reads and recursion");
    d::stop();d::start();logs.clear();identityCalls=0;
    for(unsigned i=0;i<40;++i)d::observe(state.base(),state.unit,"budget",&counted_identity,true);
    require(logs.size()==32,"hard32 record budget");const auto before=identityCalls;
    d::observe(state.base(),reinterpret_cast<void*>(1),"exhausted",&counted_identity,true);
    require(identityCalls==before,"exhausted budget fast path has no identity reads");
    d::stop();d::start();entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);logs.clear();
    require(entered&&resume,"concurrency events");
    std::thread worker([&]{d::observe(state.base(),state.unit,"late",&held_identity,true);});
    require(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0,"admitted snapshot holds outside diagnostic lock");
    d::stop();SetEvent(resume);worker.join();require(logs.empty(),"teardown prevents admitted old-generation publication");CloseHandle(entered);CloseHandle(resume);
    d::start();logs.clear();reenter=true;f::apply(state.unit,nullptr,nullptr);
    require(applyCalls==4&&logs.size()==2,"native apply reentry preserved, diagnostics outer only");
#else
    require(logs.size()==1&&logs[0].find("ev=remote_facing_control stage=publish")!=std::string::npos,"default-off has only existing feature log");
#endif
    f::activeCalls=1;require(!f::uninstall()&&f::handles[0].attached&&f::originalApply,"busy detach retains real facing hooks");f::activeCalls=0;
    detachFailure=true;require(!f::uninstall()&&f::handles[1].attached&&f::originalPass,"failed detach retains native phase trampoline");detachFailure=false;
    require(f::uninstall()&&!f::originalApply&&!f::originalPass&&!f::nativeDirection,"successful detach clears owned metadata");
    require(f::uninstall(),"duplicate teardown safe");
    std::printf("PASS %u actual-facing-header checks trace=%d\n",checks,RESTORATION_REMOTE_FACING_TRACE);
}
