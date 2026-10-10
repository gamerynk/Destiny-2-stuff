#include <Windows.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include "core/logging/log.h"
#include "client/patterns/image_scan.h"

namespace fixture {
std::atomic_uint64_t tick{1000};
std::atomic_int32_t step{38};
std::atomic_bool fault{},block{},scanMissing{};
HANDLE entered{},release{};
ULONGLONG ticks() noexcept { return tick.load(); }
std::int64_t __fastcall getter() noexcept {
    if (block.load()) { SetEvent(entered); (void)WaitForSingleObject(release,2000); }
    if (fault.load()) RaiseException(0xE0001234U,0,0,nullptr);
    return step.load();
}
}
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>,const char*) noexcept {
    return fixture::scanMissing ? nullptr : reinterpret_cast<std::byte*>(&fixture::getter);
}
}
#define GetTickCount64 fixture::ticks
#include "../dawn/src/client/hooks/bootflow/world_presence.cpp"
#undef GetTickCount64
namespace b=dawn::client::hooks::bootflow;
static bool check(bool ok,const char* name) {
    if (!ok) std::fprintf(stderr,"FAIL: %s\n",name);
    return ok;
}
int main() {
    bool ok=true;
    fixture::entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    fixture::release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!fixture::entered || !fixture::release) return 2;
    ok&=check(!b::in_world(),"no boot snapshot before installation");
    ok&=check(b::install_world_presence(),"validated native getter installed");
    b::poll_world_step();
    ok&=check(b::in_world(),"native in-world step published");
    fixture::tick=1999;
    ok&=check(b::in_world(),"999ms frame remains fresh");
    fixture::tick=2000;
    ok&=check(!b::in_world(),"exact1000ms stale frame rejected");
    fixture::step=24;b::poll_world_step();
    ok&=check(!b::in_world(),"character selection not in-world");
    fixture::step=38;fixture::fault=true;b::poll_world_step();
    ok&=check(!b::in_world(),"native singleton teardown fault publishes missing step");
    fixture::fault=false;fixture::block=true;
    std::thread poll([] { b::poll_world_step(); });
    ok&=check(WaitForSingleObject(fixture::entered,2000)==WAIT_OBJECT_0,"native reader admitted before teardown");
    std::atomic_bool teardownStarted{},teardownDone{};
    std::thread teardown([&] { teardownStarted=true;b::uninstall_world_presence();teardownDone=true; });
    for (unsigned i=0;i<2000 && !teardownStarted;++i) Sleep(1);
    Sleep(30);
    ok&=check(teardownStarted && !teardownDone,"uninstall waits for admitted frame getter");
    SetEvent(fixture::release);poll.join();teardown.join();
    ok&=check(!b::in_world(),"teardown clears admitted in-world publication");
    b::poll_world_step();
    ok&=check(!b::in_world(),"late callback cannot restore retired snapshot");
    fixture::block=false;fixture::scanMissing=true;
    ok&=check(!b::install_world_presence(),"missing native reader cannot claim readiness");
    CloseHandle(fixture::entered);CloseHandle(fixture::release);
    if (ok) std::puts("PASS: world-step native frame freshness, fault boundary and teardown quiescence");
    return ok?0:1;
}
