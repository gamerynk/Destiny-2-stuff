#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "client/hooks/bootflow/entity_lifecycle_fairness.h"
#include "client/hooks/bootflow/weapon_frame_wake.h"

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace life = dawn::client::hooks::bootflow::entity_lifecycle_fairness;
namespace move = dawn::client::hooks::bootflow::entity_movement_fairness;
namespace wake = dawn::client::hooks::bootflow::remote_weapon_native_wake;
namespace frame = dawn::client::hooks::bootflow::weapon_frame_wake;
namespace policy = dawn::client::hooks::bootflow::entity_movement_policy;
template<class T> void put(void* p, std::size_t at, T value) { std::memcpy(static_cast<std::byte*>(p)+at, &value, sizeof value); }
bool check(bool v, const char* name) { if (!v) std::fprintf(stderr,"FAIL: %s\n",name); return v; }
struct Lifecycle {
    static constexpr std::uintptr_t image=0x10000000;
    std::array<std::byte,0xD000> manager{};
    std::array<std::byte,0x70*8> rows{};
    std::array<std::array<std::byte,0x58>,8> aux{};
    std::array<std::byte,0x40> lane{};
    Lifecycle() {
        life::imageBase=image;move::table=rows.data();
        put(lane.data(),0,image+0x1C9ADD8);put(lane.data(),9,std::uint8_t{1});put(lane.data(),10,std::uint8_t{1});
        put(lane.data(),0x10,manager.data());put(manager.data(),0xC520,std::uint32_t{0xFF});
        for(std::uint32_t i=0;i<8;++i) {
            auto* r=rows.data()+i*0x70;
            put(manager.data(),0x114+i*6,static_cast<std::int16_t>(i));
            put(r,1,std::uint8_t{255});put(r,8,0x10000U+i);put(r,0xC,UINT32_MAX);
            put(r,0x44,UINT32_MAX);put(r,0x48,UINT32_MAX);put(r,0x50,std::uint16_t{4});put(r,0x60,aux[i].data());
            put(aux[i].data(),4,std::uint32_t{1});put(aux[i].data(),0x38,std::uint64_t{100});put(aux[i].data(),0x50,std::uint16_t{0x84});
        }
    }
};
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
unsigned wakeCalls{},shotCalls{},pendingCalls{};
void* requestedRoot{};const void* requestedBinding{};
void __fastcall fakeWake(void* root,const void* binding,std::uint8_t mode) noexcept {
    if(mode==1) ++wakeCalls;requestedRoot=root;requestedBinding=binding;SetLastError(987);
}
void __fastcall fakeShot(void*,const void*,const void*,float) noexcept {++shotCalls;SetLastError(123);}
std::uint8_t __fastcall fakePending(void* component,const void*) noexcept {
    ++pendingCalls;frame::shot(component,nullptr,nullptr,0);SetLastError(456);return 7;
}
int main() {
    bool ok=true;Lifecycle f;
    constexpr auto packed=(1U<<29)|(2U<<27);
    ok &= check(life::eligible(f.lane.data(),packed,{true,150}),"native pending root removal recognized at 50 ms");
    ok &= check(!life::eligible(f.lane.data(),packed,{true,149}) && !life::eligible(f.lane.data(),packed,{false,200})
        && !life::eligible(f.lane.data(),packed,{true,99}),"young/future/unknown-clock removals remain native");
    for(std::uint32_t ch=0;ch<3;++ch) ok &= check(life::eligible(f.lane.data(),(packed&~(3U<<29))|(ch<<29),{true,200},ch),"exact supported channel retained");
    auto* root=f.rows.data();auto* child=root+0x70;
    put(root,1,std::uint8_t{0});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"remote ownership denied");put(root,1,std::uint8_t{255});
    put(root,8,std::uint32_t{0x10001});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"wrong generation/index denied");put(root,8,std::uint32_t{0x10000});
    put(f.aux[0].data(),4,std::uint32_t{2});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"wrong recipient denied");put(f.aux[0].data(),4,std::uint32_t{1});
    put(f.aux[0].data(),0x50,std::uint16_t{0x85});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"creation cannot masquerade as removal");
    put(f.aux[0].data(),0x50,std::uint16_t{0x280});put(root,0x50,std::uint16_t{0});put(root,0x48,std::uint32_t{0x10001});put(child,0xC,std::uint32_t{0x10000});
    ok &= check(life::eligible(f.lane.data(),packed,{true,200}),"child removal promotes original root dependency group");
    put(child,0xC,std::uint32_t{0x20000});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"foreign child parent denied");put(child,0xC,std::uint32_t{0x10000});
    put(child,0x44,std::uint32_t{0x10001});ok &= check(!life::eligible(f.lane.data(),packed,{true,200}),"cyclic dependency rejected despite finding a removal");put(child,0x44,UINT32_MAX);
    std::size_t budget=0;ok &= check(!life::eligible(f.lane.data(),packed,{true,200},1,&budget),"exhausted traversal budget denied");
    auto output=packed;const auto savedRows=f.rows;const auto savedAux=f.aux;
    ok &= check(life::apply(f.lane.data(),1,2,1,1,&output,{true,200})==1
        && (output&policy::kPriorityMask)==policy::kPriorityMask && (output&~policy::kPriorityMask)==packed
        && f.rows==savedRows && f.aux==savedAux,"lifecycle changes priority only, no removal/ACK mutations");
    put(root,0x48,UINT32_MAX);put(root,0x50,std::uint16_t{4});put(f.aux[0].data(),0x50,std::uint16_t{0x84});put(child,0xC,UINT32_MAX);
    std::array<std::uint32_t,8> batch{};for(std::uint32_t i=0;i<8;++i)batch[i]=packed|i;
    ok &= check(life::apply(f.lane.data(),1,2,8,8,batch.data(),{true,200})==4 && batch[4]==(packed|4U),"removals have a bounded four-entry burst");

    Weapon w;ok &= check(w.image!=nullptr,"weapon fixture allocated");if(!w.image)return 1;
    wake::Request request{};
    ok &= check(wake::prepare(w.component,w.base(),request) && !request.authority && request.root==w.root
        && request.binding==w.rows.data()+0x8210,"remote cooling weapon resolves exact native frame binding");
    put(w.component,0x16F9,std::uint8_t{0});ok &= check(!wake::prepare(w.component,w.base(),request),"idle remote weapon stays native");put(w.component,0x16F9,std::uint8_t{2});
    put(w.component,0x16FC,0.0F);ok &= check(!wake::prepare(w.component,w.base(),request),"zero cooldown does not wake remote work");put(w.component,0x16FC,0.5F);
    put(w.root,0x860,std::uint32_t{2});ok &= check(!wake::prepare(w.component,w.base(),request),"inactive weapon cannot inherit selected weapon wake");put(w.root,0x860,std::uint32_t{1});
    put(w.component,0x98,std::uint32_t{2});ok &= check(!wake::prepare(w.component,w.base(),request),"recycled component owner rejected");put(w.component,0x98,std::uint32_t{1});
    put(w.suppression.data(),0,std::uint32_t{8});ok &= check(!wake::prepare(w.component,w.base(),request),"native suppression respected");put(w.suppression.data(),0,std::uint32_t{0});
    w.local(true);
    ok &= check(!wake::prepare(w.component,w.base(),request) && !wake::prepare(w.component,w.base(),request,true),"local shots cannot schedule duplicate cooldown updates");
    put(w.component,0x1050,std::numeric_limits<float>::quiet_NaN());ok &= check(!wake::prepare(w.component,w.base(),request,true),"invalid budget denied");put(w.component,0x1050,1.0F);
    const auto savedWeapon=w.rows;
    wake::imageBase=w.base();wake::nativeWake=&fakeWake;SetLastError(314);
    ok &= check(!wake::request(w.component,true) && wakeCalls==0 && GetLastError()==314 && savedWeapon==w.rows,
        "local emission budget stays entirely on native simulation path");
    w.local(false);
    ok &= check(wake::request(w.component,true) && wakeCalls==1 && requestedRoot==w.root
        && requestedBinding==w.rows.data()+0x8210 && GetLastError()==314 && savedWeapon==w.rows,
        "request invokes native queue once without editing weapon state or last error");
    w.local(true);
    frame::originalPending=&fakePending;frame::originalShot=&fakeShot;frame::enabled=true;
    wakeCalls=0;
    ok &= check(frame::pending(w.component,nullptr)==7 && pendingCalls==1 && shotCalls==1 && wakeCalls==0
        && GetLastError()==456 && frame::nesting==0 && frame::activeCalls==0 && savedWeapon==w.rows,
        "nested local shot executes once without a second weapon update");
    w.local(false);frame::shot(w.component,nullptr,nullptr,0);
    ok &= check(shotCalls==2 && wakeCalls==1 && GetLastError()==123 && savedWeapon==w.rows,
        "remote cooling shot recovery remains available");
    frame::enabled=false;wake::disable();move::table=nullptr;life::imageBase=0;
    if(ok)std::puts("combat lifecycle and native weapon wake: PASS");return ok?0:1;
}
