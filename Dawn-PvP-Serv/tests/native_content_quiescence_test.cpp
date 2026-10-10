#include <Windows.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include "client/hooks/network/content_config/content_native_quiescence.h"
#include "client/patterns/image_scan.h"
#include "client/process/freeze/client_process_freeze.h"
#include "core/logging/log.h"
namespace {
std::byte* fixture{};
HANDLE workerHandle{};
bool failHold{}, invalidContext{};
std::atomic_bool workerEntered{}, workerRelease{};
unsigned holds{}, releases{};
__declspec(noinline) void native_parser_frame() {
    workerEntered=true;
    while(!workerRelease.load()) Sleep(1);
}
void displacement(std::byte* code,unsigned offset,unsigned end,void* target) {
    const auto value=static_cast<std::int32_t>(static_cast<std::byte*>(target)-(code+end));
    std::memcpy(code+offset,&value,sizeof value);
}
}
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>,const char* name) noexcept {
    if(std::strstr(name,"fetch"))return fixture;
    if(std::strstr(name,"parse"))return fixture+512;
    if(std::strstr(name,"tick"))return fixture+4096;
    return fixture+2048;
}
}
namespace dawn::core::log {void write(Channel,Level,std::string_view) noexcept{}}
namespace dawn::client::process::freeze {
bool hold(Held& held) noexcept {
    ++holds;held={};
    if(failHold)return false;
    if(invalidContext){held.count=1;held.handles[0]=reinterpret_cast<HANDLE>(1);return true;}
    if(workerHandle){assert(SuspendThread(workerHandle)!=DWORD(-1));held.handles[0]=workerHandle;held.count=1;}
    return true;
}
void release(Held& held) noexcept {
    ++releases;
    if(held.count && !invalidContext)assert(ResumeThread(held.handles[0])!=DWORD(-1));
    held={};
}
}
int main(){
    namespace work=dawn::client::hooks::network::content_config::native_work;
    namespace gate=dawn::client::hooks::network::content_config::gate;
    fixture=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(fixture);
    auto* state=reinterpret_cast<LONG*>(fixture+2304);
    auto* enable=fixture+2312;
    constexpr unsigned char fetchEnd[]{0x48,0x8b,0x5c,0x24,0x50,0x48,0x8b,0x74,0x24,0x58,0x48,0x83,0xc4,0x40,0x5f,0xc3};
    constexpr unsigned char parseEnd[]{0x48,0x81,0xc4,0xb0,0x0c,0,0,0x5d,0xc3};
    std::memcpy(fixture+0x9a,fetchEnd,sizeof fetchEnd);std::memcpy(fixture+512+0x571,parseEnd,sizeof parseEnd);
    constexpr unsigned char tickEnd[]{0x48,0x81,0xc4,0x60,0x07,0,0,0x5f,0x5e,0x5d,0xc3};
    std::memcpy(fixture+4096+0x6a1,tickEnd,sizeof tickEnd);
    fixture[4096+0x1e3]=std::byte{0xe8};displacement(fixture+4096,0x1e4,0x1e8,fixture);
    fixture[4096+0x71b]=std::byte{0xe9};displacement(fixture+4096,0x71c,0x720,fixture+4096+0x341);
    fixture[52]=std::byte{0xc7};fixture[53]=std::byte{5};
    fixture[512+0xb7]=std::byte{0x80};fixture[512+0xb8]=std::byte{0x3d};
    displacement(fixture+2048,2,6,state);displacement(fixture+512,36,41,state);
    displacement(fixture,54,62,state);displacement(fixture+512,0xb9,0xbe,enable);
    work::Metadata metadata{};
    assert(work::resolve(enable,metadata)&&metadata.state==state);
    assert(!work::resolve(enable+1,metadata)&&!metadata.state);
    displacement(fixture+2048,2,6,state+1);assert(!work::resolve(enable,metadata));
    displacement(fixture+2048,2,6,state);
    fixture[0xa9]=std::byte{0};assert(!work::resolve(enable,metadata));fixture[0xa9]=std::byte{0xc3};
    assert(work::resolve(enable,metadata));
    std::array<DWORD64,300> stack{};
    fixture[3000]=std::byte{0xc3};
    CONTEXT context{};context.Rip=reinterpret_cast<DWORD64>(fixture+3000);context.Rsp=reinterpret_cast<DWORD64>(stack.data());
    assert(!work::stack_is_idle(context,metadata)); // private executable code has no proven ABI
    context.Rip=reinterpret_cast<DWORD64>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtYieldExecution"));
    assert(context.Rip && work::stack_is_idle(context,metadata));
    context.Rip=metadata.parseBegin;assert(!work::stack_is_idle(context,metadata));
    context.Rip=metadata.tickBegin;assert(!work::stack_is_idle(context,metadata));
    context.Rip=reinterpret_cast<DWORD64>(fixture+3000);context.Rsp=1;assert(!work::stack_is_idle(context,metadata));
    context.Rsp=reinterpret_cast<DWORD64>(stack.data());context.Rip=reinterpret_cast<DWORD64>(stack.data());
    assert(!work::stack_is_idle(context,metadata));
    context.Rip=reinterpret_cast<DWORD64>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtYieldExecution"));stack.fill(context.Rip);
    assert(!work::stack_is_idle(context,metadata));stack.fill(0);
    gate::Ownership ownership{};*enable=std::byte{1};assert(gate::disable(enable,ownership));
    *state=0;const auto before=holds;assert(!work::restore_when_idle(metadata,ownership)&&holds==before&&ownership.address);
    *state=1;failHold=true;assert(!work::restore_when_idle(metadata,ownership)&&ownership.address);failHold=false;
    invalidContext=true;assert(!work::restore_when_idle(metadata,ownership)&&ownership.address);invalidContext=false;
    std::thread worker(native_parser_frame);workerHandle=worker.native_handle();
    while(!workerEntered.load())std::this_thread::yield();
    DWORD64 image{};
    const auto function=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&native_parser_frame),&image,nullptr);
    assert(function);metadata.parseBegin=image+function->BeginAddress;metadata.parseEnd=image+function->EndAddress;
    assert(!work::restore_when_idle(metadata,ownership)&&ownership.address&&*enable==std::byte{0});
    workerRelease=true;worker.join();workerHandle=nullptr;
    assert(work::restore_when_idle(metadata,ownership)&&!ownership.address&&*enable==std::byte{1});
    assert(releases>0);
    assert(VirtualFree(fixture,0,MEM_RELEASE));
    std::puts("PASS: native metadata identity, pending parse retention, bounded stack failures, nested native parser frame exclusion, freeze failure and successful retry");
}
