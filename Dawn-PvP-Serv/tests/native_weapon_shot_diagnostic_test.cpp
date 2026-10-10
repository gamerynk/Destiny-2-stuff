#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
static HMODULE fakeModule{};
static HMODULE fake_module(LPCWSTR) noexcept { return fakeModule; }
#define GetModuleHandleW fake_module
#include "../dawn/src/client/hooks/bootflow/weapon_frame_wake.h"
#undef GetModuleHandleW
namespace frame=dawn::client::hooks::bootflow::weapon_frame_wake;
namespace wake=dawn::client::hooks::bootflow::remote_weapon_native_wake;
namespace detour=dawn::client::hooking::detour;
static unsigned checks{},attachCalls{},detachCalls{},nativeShots{},nativePending{},nativeApply{},wakeCalls{};
static bool failAttach{},failDetach{},mutateIdentity{};
static std::vector<std::string> logs;
namespace dawn::core::log {void write(Channel,Level,std::string_view message) noexcept {logs.emplace_back(message);SetLastError(999);}}
template<class T> void put(void* p,std::size_t at,T value) {std::memcpy(static_cast<std::byte*>(p)+at,&value,sizeof(value));}
static void require(bool value,const char* label) {++checks;if(!value){std::fprintf(stderr,"FAIL:%s\n",label);std::exit(1);}}
struct Weapon {
    alignas(16) std::array<std::byte,0xC000> rows{};
    std::array<std::byte,0x40> table{};
    std::array<std::byte,8> tableRoot{};
    std::array<std::byte,1024> suppression{};
    std::byte* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x2700000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    std::byte* root=rows.data()+0x4000;
    std::byte* component=root+0xD70;
    Weapon() {
        if(!image) return;
        put(image,0x2439C70,tableRoot.data());put(tableRoot.data(),0,table.data());
        put(table.data(),8,rows.data());put(table.data(),0x30,std::uint32_t{0x4000});
        put(image,0x1FBA9C8,suppression.data());
        put(root,0,std::uint32_t{0x80000002});put(root,0x24,std::uint32_t{1});put(root,0x2C,std::uint32_t{3});
        put(root,0x7B0,std::uint8_t{8});put(root,0x860,std::uint32_t{1});put(root,0x868,std::uint64_t{0xD70});
        put(root,0x6D0,std::uint32_t{0x80000002});put(root,0x6D4,std::uint32_t{0x80803953});put(root,0x6D8,std::uint64_t{0x200});
        put(component,0,std::uint32_t{0x80000002});put(component,4,std::uint32_t{0x80803865});put(component,0x98,std::uint32_t{1});
        put(component,0x16F9,std::uint8_t{2});put(component,0x16FC,0.5F);put(component,0x1050,1.0F);
    }
    ~Weapon() { if(image) VirtualFree(image,0,MEM_RELEASE); }
    std::uintptr_t base() const {return reinterpret_cast<std::uintptr_t>(image);}
    void local(bool v) {put(image,0x26BE0E0,std::uint32_t{v?8U:0U});}
};

static Weapon* fixture{};
static void __fastcall native_shot(void*,const void*,const void*,float) noexcept {++nativeShots;SetLastError(123);}
static std::uint8_t __fastcall native_pending(void* component,const void*) noexcept {++nativePending;frame::shot(component,nullptr,nullptr,0);SetLastError(456);return 7;}
static void __fastcall native_apply(void*,const void*) noexcept {++nativeApply;SetLastError(789);}
static void __fastcall fake_wake(void*,const void*,std::uint8_t mode) noexcept {
    if(mode==1)++wakeCalls;
    if(mutateIdentity)put(fixture->component,0x98,std::uint32_t{2});
    SetLastError(987);
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> outputs) noexcept {
    ++attachCalls;require(specs.size()==outputs.size(),"transaction sizes");if(failAttach)return false;
    for(std::size_t i=0;i<specs.size();++i){void* original{};
      if(specs[i].replacement==reinterpret_cast<void*>(&frame::shot))original=reinterpret_cast<void*>(&native_shot);
      else if(specs[i].replacement==reinterpret_cast<void*>(&frame::pending))original=reinterpret_cast<void*>(&native_pending);
      else if(specs[i].replacement==reinterpret_cast<void*>(&frame::apply))original=reinterpret_cast<void*>(&native_apply);
      require(original!=nullptr,"only actual owned entries attached");outputs[i]={original,specs[i].replacement,true};
    }return true;
}
bool install(const Spec& spec,Handle& output) noexcept {return install({&spec,1},{&output,1});}
UninstallResult uninstall(std::span<Handle> handles,std::span<const ProtectedCodeEntry> entries,IdleCheck idle) noexcept {
    ++detachCalls;require(entries.size()==3,"all wrapper code protected");if(!idle())return UninstallResult::protectedCodeActive;if(failDetach)return UninstallResult::failed;
    for(auto& handle:handles)handle={};return UninstallResult::removed;
}
}
static void layout(Weapon& w) {
 auto* image=w.image;
 const std::array<std::uint8_t,14> apply{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x4C,0x8B,0x42,0x08};
 const std::array<std::uint8_t,15> pending{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
 const std::array<std::uint8_t,5> shot{0xE9,0xA5,0x06,0x2B,0x04};const std::array<std::uint8_t,11> body{0x49,0x8B,0xF8,0x48,0x8B,0xD9,0x8B,0x09,0x0F,0x28,0xF3};
 const std::array<std::uint8_t,17> wakeHead{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x50,0x0F,0x10,0x01,0x41,0x0F,0xB6,0xF8};
 const std::array<std::uint8_t,15> call{0x48,0x8B,0xC8,0x48,0x8D,0x53,0x10,0x41,0xB0,0x01,0xE8,0x03,0x54,0x76,0xFF};
 std::memcpy(image+0xCC4010,apply.data(),apply.size());std::memcpy(image+0xCCF6A0,pending.data(),pending.size());std::memcpy(image+0xCB48C0,shot.data(),shot.size());std::memcpy(image+0xCB48FB,body.data(),body.size());std::memcpy(image+0x44E3C0,wakeHead.data(),wakeHead.size());std::memcpy(image+0xCE8FAE,call.data(),call.size());
 put(image,0x1D03650,reinterpret_cast<std::uintptr_t>(image+0xCCF6A0));put(image,0x1D03630,reinterpret_cast<std::uintptr_t>(image+0xCC4010));put(image,0x1D04130,reinterpret_cast<std::uintptr_t>(image+0xD0BF80));fakeModule=reinterpret_cast<HMODULE>(image);
}
int main() {
 Weapon w;require(w.image!=nullptr,"fixture allocation");fixture=&w;layout(w);
 frame::install();constexpr unsigned expected=1+!RESTORATION_NATIVE_WEAPON_PENDING_OMISSION+!RESTORATION_NATIVE_WEAPON_APPLY_OMISSION;
 unsigned count{};for(const auto& h:frame::handles)count+=h.attached;require(count==expected,"exact selected physical hooks");require(frame::handles[2].attached&&frame::originalShot.load()==&native_shot,"retained shot slot2");
 require(frame::handles[0].attached==!RESTORATION_NATIVE_WEAPON_PENDING_OMISSION,"pending selected independently");require(frame::handles[1].attached==!RESTORATION_NATIVE_WEAPON_APPLY_OMISSION,"apply selected independently");
 require((frame::originalPending.load()!=nullptr)==!RESTORATION_NATIVE_WEAPON_PENDING_OMISSION,"omitted pending trampoline remains null");require((frame::originalApply.load()!=nullptr)==!RESTORATION_NATIVE_WEAPON_APPLY_OMISSION,"omitted apply trampoline remains null");
 const auto installed=attachCalls;frame::install();require(attachCalls==installed,"shot-only idempotence does not reattach");wake::nativeWake=&fake_wake;
 SetLastError(11);frame::shot(w.component,nullptr,nullptr,0);require(nativeShots==1&&wakeCalls==1&&GetLastError()==123,"shot behavior and LastError preserved");
 auto old=wakeCalls;auto result=RESTORATION_NATIVE_WEAPON_PENDING_OMISSION?native_pending(w.component,nullptr):frame::pending(w.component,nullptr);
 require(result==7&&nativePending==1&&nativeShots==2&&GetLastError()==456,"nested native pending reaches retained shot exactly once");require(wakeCalls-old==(RESTORATION_NATIVE_WEAPON_PENDING_OMISSION?1U:2U),"omission removes redundant outer request only in fixture");
 if(!RESTORATION_NATIVE_WEAPON_APPLY_OMISSION) {old=wakeCalls;frame::apply(w.component,nullptr);require(nativeApply==1&&wakeCalls==old+1&&GetLastError()==789,"control apply preserved");}
#if RESTORATION_REMOTE_WEAPON_SHOT_TRACE
 logs.clear();frame::shotDiagnosticBudget=1;auto shotBefore=nativeShots;old=wakeCalls;
 frame::shot(w.component,nullptr,nullptr,0);
 require(nativeShots==shotBefore+1 && wakeCalls==old+1 && GetLastError()==123,"diagnostic retains original-before-native-request and LastError");
 require(logs.size()==1 && logs[0].find("stage=shot_request_diagnostic")!=std::string::npos && logs[0].find("queue_admission_change=unobserved")!=std::string::npos,"shot state evidence is not queue proof");
 frame::shotDiagnosticBudget=32;logs.clear();for(unsigned i=0;i<40;++i)frame::shot_request(w.component,false);
 require(logs.size()==32 && frame::shotDiagnosticBudget==0,"32 hard budget");
 logs.clear();frame::shot_request(w.component,false);require(logs.empty(),"exhausted requests remain functional without diagnostic");
 frame::shotDiagnosticBudget=1;w.local(true);old=wakeCalls;frame::shot_request(w.component,false);
 require(logs.empty() && wakeCalls==old && frame::shotDiagnosticBudget==1,"local authority never diagnostic-selected or queued");w.local(false);
 mutateIdentity=true;frame::shot_request(w.component,false);mutateIdentity=false;
 require(logs.size()==1 && logs[0].find("after_qualified=0")!=std::string::npos,"changed identity labeled unqualified");put(w.component,0x98,std::uint32_t{1});
 logs.clear();frame::shotDiagnosticBudget=0;
#endif
 frame::activeCalls=1;require(!frame::uninstall()&&frame::enabled&&frame::handles[2].attached,"busy detach retains admission and metadata");frame::activeCalls=0;failDetach=true;require(!frame::uninstall()&&frame::enabled&&frame::originalShot.load()==&native_shot,"failed detach remains retryable");failDetach=false;
 require(frame::uninstall()&&!frame::enabled&&!frame::originalShot&&!frame::originalPending&&!frame::originalApply&&!wake::nativeWake&&!wake::imageBase,"successful teardown clears all owned metadata");require(frame::uninstall(),"duplicate teardown safe");
 failAttach=true;frame::install();require(!frame::enabled&&!frame::handles[2].attached&&!wake::nativeWake,"failed attach leaves no partial enabled state");failAttach=false;
 put(w.image,0x1D03650,std::uintptr_t{});old=attachCalls;frame::install();require(attachCalls==old&&!frame::enabled,"native callback signature/slot mismatch refuses selected trial");layout(w);frame::install();require(frame::enabled&&frame::uninstall(),"clean reinstall after prior failure");
 std::printf("PASS %u checks pending=%d apply=%d shot_trace=%d hooks=%u\n",checks,RESTORATION_NATIVE_WEAPON_PENDING_OMISSION,RESTORATION_NATIVE_WEAPON_APPLY_OMISSION,RESTORATION_REMOTE_WEAPON_SHOT_TRACE,expected);
}
