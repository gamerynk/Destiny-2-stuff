#include <Windows.h>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include "client/hooks/bootflow/special_ammo_handoff.h"
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace P = dawn::client::hooks::bootflow::special_ammo_handoff;
namespace D = dawn::client::hooking::detour;
bool attachFail{}; unsigned attachCalls{}, detachCalls{}, checks{};
std::vector<D::Spec> seen;
std::vector<D::ProtectedCodeEntry> protectedSeen;
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> outputs) noexcept {
 ++attachCalls;seen.assign(specs.begin(),specs.end());
 if(attachFail)return false;
 if(specs.size()!=outputs.size())std::abort();
 for(std::size_t i=0;i<specs.size();++i)outputs[i]={specs[i].target,specs[i].replacement,true};
 return true;
}
UninstallResult uninstall(std::span<Handle> outputs,std::span<const ProtectedCodeEntry> entries,IdleCheck idle) noexcept {
 ++detachCalls;protectedSeen.assign(entries.begin(),entries.end());
 if(!idle())return UninstallResult::protectedCodeActive;
 for(auto& handle:outputs)handle={};
 return UninstallResult::removed;
}
}
template<class T> void put(void* ptr,std::size_t off,T value){std::memcpy(static_cast<std::byte*>(ptr)+off,&value,sizeof value);}
bool check(bool x,const char* name){++checks;if(!x)std::fprintf(stderr,"FAIL %s\n",name);return x;}
int main(){
 auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3300000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 if(!image)return 2;
 {const std::uint8_t bytes[]{0x4C,0x8B,0xDC,0x56,0x41,0x54,0x41,0x55,0x48,0x81,0xEC,0xA0,0,0,0};std::memcpy(image+0xE94C30,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x8B,0x04,0x82,0x0F,0xA3,0xC8,0x0F,0x83,0x82,0x02,0,0};std::memcpy(image+0xE94C7B,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x40,0x57,0x48,0x83,0xEC,0x50,0x48,0x8B,0x05,0xAB,0x80,0x1F,0x01,0x48,0x8B,0xF9};std::memcpy(image+0xE8B030,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x40,0x53,0x48,0x83,0xEC,0x30,0x8B,0xD9,0xE8,0x33,0x3C,0xFF,0xFF,0xF6,0x00,0x02,0x74};std::memcpy(image+0x403A80,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x8B,0xCE,0xE8,0xB7,0xF7,0x6C,0xFF};std::memcpy(image+0xD342C2,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};std::memcpy(image+0xF32CD0,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x41,0xC6,0x47,0x01,0,0x49,0x8D,0x6F,0x10};std::memcpy(image+0xF33066,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x48,0x89,0x5C,0x24,0x08};std::memcpy(image+0xE8A870,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xB0,0,0,0};std::memcpy(image+0x170CAA0,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9,0x83,0xF9,0xFF};std::memcpy(image+0x403ED0,bytes,sizeof bytes);}
 {const std::uint8_t bytes[]{0x8B,0x02,0x89,0x01,0x8B,0x42,0x04,0x89,0x41,0x04,0xE9,0xC1,0xF2,0xFF,0xFF,0xCC};std::memcpy(image+0xA40F40,bytes,sizeof bytes);}
 put(image,0x1CF64E0,reinterpret_cast<std::uintptr_t>(image)+0xE8A870);
 put(image,0x1CF64E8,0xA1873B0CU);
 put(image,0x1CF64F0,reinterpret_cast<std::uintptr_t>(image)+0xE94C30);
 put(image,0x1CF64F8,0x445966ADU);
 bool ok=check(P::supported(image),"exact captured native layout accepted");
 image[0xA40F40] ^= std::byte{1};
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
 ok&=check(!P::supported(image),"changed native lookup entry fails trial layout validation");
 P::install_for_image(image);
 ok&=check(attachCalls==0 && !P::enabled && !P::nativeMethodLookup,"bad lookup fingerprint cannot attach or publish callable pointer");
#else
 ok&=check(P::supported(image),"default retains old layout contract without native lookup dependency");
#endif
 image[0xA40F40] ^= std::byte{1};
 attachFail=true;P::install_for_image(image);
 ok&=check(!P::enabled && !P::handles[0].attached && !P::nativeMethodLookup && !P::originalQuery,"failed atomic batch publishes no callable state");
 ok&=check(attachCalls==1 && seen.size()==P::hookCount,"exact requested detour count");
 attachFail=false;P::install_for_image(image);
 ok&=check(P::enabled && P::handles[0].attached && attachCalls==2,"retry attaches and enables");
 ok&=check(seen[0].target==image+0xE94C30 && seen[1].target==image+0xF32CD0,"collect and outer slots preserve ordering");
 const auto transferSlot=RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL?2U:3U;
 ok&=check(seen[transferSlot].target==image+0x170CAA0 && reinterpret_cast<void*>(P::originalTransfer.load())==image+0x170CAA0,"transfer original uses correct reduced slot");
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
 ok&=check(seen.size()==3 && !P::originalAmmoQuery && reinterpret_cast<void*>(P::nativeMethodLookup.load())==image+0xA40F40,"observer absent and native lookup published after attach");
 for(const auto& spec:seen)ok&=check(spec.target!=image+0xE8A870,"no hidden observer target");
#else
 ok&=check(seen.size()==4 && seen[2].target==image+0xE8A870 && reinterpret_cast<void*>(P::originalAmmoQuery.load())==image+0xE8A870 && !P::nativeMethodLookup,"default observer remains and trial lookup stays unset");
#endif
 P::install_for_image(image);ok&=check(attachCalls==2,"idempotent install");
 P::activeCalls=1;ok&=check(!P::uninstall() && P::enabled && P::handles[0].attached,"active-call teardown refuses and restores enabled owner");
 ok&=check(protectedSeen.size()==P::hookCount && protectedSeen[transferSlot].address==reinterpret_cast<void*>(&P::transfer),"quiescence entries use exact reduced ordering");
 P::activeCalls=0;ok&=check(P::uninstall() && !P::enabled && !P::handles[0].attached && !P::nativeMethodLookup && !P::originalQuery && !P::originalTransfer,"idle teardown clears all native state");
 const auto prior=detachCalls;ok&=check(P::uninstall() && detachCalls==prior,"already detached cleanup is idempotent");
 P::install_for_image(image);ok&=check(P::enabled && attachCalls==3,"new install after teardown can retry");
 ok&=check(P::uninstall(),"final controlled teardown");
 VirtualFree(image,0,MEM_RELEASE);
 std::printf("%s %u actual production lifecycle checks trial=%d hooks=%zu; controlled detour leaves only\n",ok?"PASS":"FAIL",checks,RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL,P::hookCount);
 return ok?0:1;
}
