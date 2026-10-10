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
static bool failAttach{},failDetach{};
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

static void __fastcall native_shot(void*,const void*,const void*,float) noexcept {++nativeShots;SetLastError(123);}
static std::uint8_t __fastcall native_pending(void* component,const void*) noexcept {++nativePending;frame::shot(component,nullptr,nullptr,0);SetLastError(456);return 7;}
static void __fastcall native_apply(void*,const void*) noexcept {++nativeApply;SetLastError(789);}
static void __fastcall fake_wake(void*,const void*,std::uint8_t mode) noexcept {
    if(mode==1)++wakeCalls;
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
 Weapon w;require(w.image!=nullptr,"fixture allocation");layout(w);
 const auto before=w.rows;
 frame::install();frame::install();
 unsigned count{};for(const auto& h:frame::handles)count+=h.attached;
 require(count==0&&attachCalls==0,"all legacy selection profiles install zero hooks");
 require(!frame::enabled&&!frame::originalShot&&!frame::originalPending&&!frame::originalApply,
         "native profile has no admission or trampoline");
 require(!wake::nativeWake&&!wake::imageBase,"no direct native wake fallback configured");
 require(w.rows==before&&nativeShots==0&&nativePending==0&&nativeApply==0&&wakeCalls==0,
         "install preserves native fixture and invokes no gameplay callbacks");
 require(!logs.empty()&&logs.back().find("hooks=0 compatibility=retired")!=std::string::npos,
         "runtime log reports retired profile honestly");
 require(frame::uninstall()&&frame::uninstall()&&detachCalls==0,"inert teardown is idempotent");
 fakeModule=nullptr;frame::install();require(attachCalls==0&&!frame::enabled,
         "missing native image cannot activate fallback");
 const auto legacyWake=&fake_wake;require(legacyWake!=nullptr,"legacy evidence retained");
 std::printf("PASS %u native weapon retirement checks hooks=0\n",checks);
}
