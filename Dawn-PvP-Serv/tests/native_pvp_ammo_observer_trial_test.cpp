#include <Windows.h>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#ifndef SELF_TRIAL
#define SELF_TRIAL 0
#endif
#include "client/hooks/bootflow/special_ammo_handoff.h"
namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace P = dawn::client::hooks::bootflow::special_ammo_handoff;
template<class T> void put(void* p, std::size_t off, T v) { std::memcpy(static_cast<std::byte*>(p)+off,&v,sizeof v); }
constexpr std::uint32_t legacy=0x08FAA00D, sim=0x2AFBA017;
struct Fixture {
 std::byte* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3300000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 std::vector<std::byte> units=std::vector<std::byte>(4096*0xE0);
 std::array<std::byte,960> players{};
 std::array<std::byte,0x388> brick{}, generic{};
 std::array<std::byte,24> header{};
 std::array<std::byte,64> table{};
 Fixture() {
  put(image,0x1F93428,units.data());put(image,0x1F93430,0xE0U);put(image,0x1F93434,4096U);
  put(units.data(),(legacy&0x1FFF)*0xE0+0xC,legacy);put(units.data(),(legacy&0x1FFF)*0xE0+0x4C,0x7AF9E8BCU);
  put(image,0x1F90E18,players.data());put(image,0x1F90E20,480U);put(image,0x1F90E24,2U);
  put(players.data(),0x40,3U);put(players.data(),0x54,42U);
  put(brick.data(),4,0x80804494U);put(brick.data(),0x2C,legacy);
  put(brick.data(),0x2D0,std::uint8_t{2});put(brick.data(),0x2DC,std::uint8_t{1});put(brick.data(),0x2DD,std::uint8_t{2});put(brick.data(),0x2D8,0.024F);
  put(brick.data(),0x30,0x80804491U);put(brick.data(),0x34,3U);put(brick.data(),0x38,0U);put(brick.data(),0x40,std::uint64_t{0});
  generic=brick;put(generic.data(),4,0x80804FBCU);
  put(image,0x3280378,0x80804491U);put(image,0x3280380,reinterpret_cast<std::uintptr_t>(image)+0x1CF64B0);put(image,0x3280388,7U);
  put(image,0x2439C70,header.data());put(header.data(),0,table.data());put(table.data(),8,brick.data());put(table.data(),0x30,0x400U);
 }
 ~Fixture(){if(image)VirtualFree(image,0,MEM_RELEASE);}
};
Fixture* f{};
unsigned originals{}, bodies{}, handoffs{}, lookups{}, checks{};
bool inventory=true,geometry=true,callback=true;
int mutation{};
DWORD errorSeen{};
std::uint32_t __fastcall simulation(std::uint32_t token) noexcept {return token==legacy ? sim : UINT32_MAX;}
void __fastcall handoff(std::uint32_t) noexcept {++handoffs;SetLastError(800);}
void __fastcall body(void*,std::uint32_t,void*,void* result) noexcept {++bodies;put(result,1,std::uint8_t(inventory?2:0));SetLastError(456);}
#if SELF_TRIAL
void __fastcall lookup(void* out,const void*) noexcept {++lookups;put(out,8,reinterpret_cast<std::uintptr_t>(f->image)+0xE8A870);SetLastError(789);}
#endif
void __fastcall original(void*,std::uint32_t player,std::uint8_t flags,void* profile,std::uintptr_t movement,
 const void* position,const void* origin,const void* direction,bool update,void* result) noexcept {
 ++originals;errorSeen=GetLastError();
 if(flags!=0||profile!=reinterpret_cast<void*>(0x111)||movement!=0x222||position!=reinterpret_cast<void*>(0x333)||origin!=reinterpret_cast<void*>(0x444)||direction!=reinterpret_cast<void*>(0x555)||!update) {
  if(flags==0 && update) std::abort();
 }
 put(result,1,std::uint8_t{2});put(result,0xC,std::uint8_t{1});
 if(callback) {
#if SELF_TRIAL
  body(f->brick.data(),player,profile,result);
#else
  P::ammo_query(f->brick.data(),player,profile,result);
#endif
 }
 if(mutation==1)put(f->brick.data(),0x34,4U);
 if(mutation==2)put(f->brick.data(),0x2D8,0.0F);
 if(mutation==3)put(f->players.data(),0x54,44U);
 if(!geometry)put(result,1,std::uint8_t{0});
 SetLastError(654);
}
bool check(bool x,const char* label){++checks;if(!x)std::fprintf(stderr,"FAIL %s\n",label);return x;}
int main(){
 bool ok=true;
 struct Case{const char* name;bool nonself,inv,geo,call;int change;bool oldQual,newQual;std::uint8_t flags;bool update;};
 const std::array<Case,11> cases{{
  {"self accepted",false,true,true,true,0,true,true,0,true},
  {"inventory rejected",false,false,true,true,0,false,false,0,true},
  {"geometry rejected",false,true,false,true,0,false,false,0,true},
  {"non-self native bound target: covered",true,true,true,true,0,true,true,0,true},
  {"descriptor changed after callback: coverage gap",false,true,true,true,1,true,false,0,true},
  {"amount changed after callback: covered",false,true,true,true,2,true,true,0,true},
  {"player body changed",false,true,true,true,3,false,false,0,true},
  {"invalid tag cannot infer callback from stale2",false,true,true,false,0,false,false,0,true},
  {"preview flags",false,true,true,true,0,false,false,1,true},
  {"update false",false,true,true,true,0,false,false,0,false},
  {"already owned accepted lease",false,true,true,true,0,true,true,0,true}
 }};
 for(const auto& c:cases){
  Fixture fixture;f=&fixture;if(!f->image)return 2;
  P::leases={};P::retries={};P::qualified=0;P::requests=0;P::imageBase=reinterpret_cast<std::uintptr_t>(f->image);
  P::enabled=true;P::originalQuery=&original;P::originalAmmoQuery=&body;P::nativeSimulation=&simulation;P::nativeHandoff=&handoff;
#if SELF_TRIAL
  P::nativeMethodLookup=&lookup;
#endif
  originals=bodies=handoffs=lookups=0;inventory=c.inv;geometry=c.geo;callback=c.call;mutation=c.change;
  if(!c.call)put(f->brick.data(),0x30,0U);
  if(std::strcmp(c.name,"already owned accepted lease")==0)put(f->image,0x26BE0E0+((legacy&0x1FFF)>>5)*4,1U<<((legacy&0x1FFF)&31));
  std::array<std::byte,0x30> result{};put(result.data(),1,std::uint8_t{2});SetLastError(123);
  P::query(c.nonself?f->generic.data():f->brick.data(),0,c.flags,reinterpret_cast<void*>(0x111),0x222,reinterpret_cast<void*>(0x333),reinterpret_cast<void*>(0x444),reinterpret_cast<void*>(0x555),c.update,result.data());
  const bool expected=SELF_TRIAL ? c.newQual : c.oldQual;
  ok&=check((P::qualified.load()!=0)==expected,c.name);
  ok&=check(originals==1&&bodies==unsigned(c.call),"native bodies exactly once");
  ok&=check(errorSeen==123&&GetLastError()==654,"native LastError survives lookup and policy");
  ok&=check(P::queryScope==nullptr&&P::activeCalls==0,"scope and active-call teardown");
  ok&=check(P::leases[sim&0x1FFF].key.simulation==(expected?sim:UINT32_MAX),"only admitted lease is published");
 }
 std::printf("%s %u actual-wrapper checks variant=%d; modeled lookup/eligibility leaves, no game calls\n",ok?"PASS":"FAIL",checks,SELF_TRIAL);
 return ok?0:1;
}
