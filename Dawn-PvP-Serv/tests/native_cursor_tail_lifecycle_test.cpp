#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "../dawn/src/client/hooks/cursor/cursor_hook_lifecycle.cpp"
namespace f {
unsigned checks{}, attaches{}, detaches{}, resets{}, policies{};
bool ready{true}, configured{true}, safe{true}, ownerDone{}, failAttach{}, failDetach{};
void check(bool ok, const char* why) {
    ++checks; if (!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(1); }
}
}
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::client::hooks::polled_input {
bool native_pause_available() noexcept { return true; }
bool native_key_recovery_safe() noexcept { return f::safe; }
}
namespace dawn::client::hooks::cursor::tail {
void bootstrap() noexcept {}
bool configured() noexcept { return f::configured; }
bool omission_ready() noexcept { return f::ready; }
bool prepare_shutdown() noexcept { return f::ownerDone; }
bool prepare_retirement() noexcept { return f::ownerDone; }
void reset_after_uninstall() noexcept { ++f::resets; }
}
namespace dawn::client::hooks::cursor {
std::array<hooking::detour::Handle,kHandleCount> g_handles{};
std::array<void*,kHandleCount> g_targets{};
BOOL WINAPI set_cursor_pos(int,int) noexcept { return TRUE; }
BOOL WINAPI clip_cursor(const RECT*) noexcept { return TRUE; }
void apply_policy(bool) noexcept { ++f::policies; }
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> handles) noexcept {
    ++f::attaches; if (f::failAttach) return false;
    for (std::size_t i=0;i<handles.size();++i) handles[i]={specs[i].target,specs[i].replacement,true};
    return true;
}
bool uninstall(std::span<Handle> handles) noexcept {
    ++f::detaches; if (f::failDetach) return false;
    for (auto& h:handles) h={}; return true;
}
}
int main() {
    namespace p=dawn::client::hooks::cursor;
    f::check(p::install()&&p::is_installed()&&p::native_clip_guard_attached(),"startup real guard owns fallback");
    f::safe=false; p::service_native_cursor_tail();
    f::check(f::detaches==0&&p::g_handles[1].attached,"unsafe native worker cannot detach");
    f::safe=true; f::failDetach=true; p::service_native_cursor_tail();
    f::check(p::native_clip_guard_attached()&&p::g_handles[1].attached,"selection detach failure preserves real guard");
    f::failDetach=false; p::service_native_cursor_tail();
    f::check(!p::native_clip_guard_attached()&&!p::g_handles[1].attached&&p::is_installed(),"qualified selection removes only Clip slot");
    p::request_native_clip_guard(); f::failAttach=true; p::service_native_cursor_tail();
    f::check(!p::native_clip_guard_attached()&&p::g_clipRecovery.load(),"failed fallback attachment retains retry request");
    f::failAttach=false; p::service_native_cursor_tail();
    f::check(p::native_clip_guard_attached()&&p::g_handles[1].attached,"fallback reports actual reattachment");
    const auto detachCount=f::detaches; p::service_native_cursor_tail();
    f::check(f::detaches==detachCount&&p::native_clip_guard_attached(),"sticky fallback cannot immediately redetach");
    p::uninstall();
    f::check(p::is_installed()&&f::resets==0&&f::policies==0,"pending owner cannot apply old policy or reset runtime");
    f::ownerDone=true; f::failDetach=true; p::uninstall();
    f::check(p::is_installed()&&f::resets==0&&p::g_clipRecovery.load()&&p::g_targets[1]&&p::g_module,
             "cleanup completed but detach failed preserves recovery/export/module");
    f::failDetach=false; p::uninstall();
    f::check(!p::is_installed()&&!p::native_clip_guard_attached()&&f::resets==1&&!p::g_clipRecovery.load()
             &&!p::g_targets[1]&&!p::g_module,"successful complete teardown alone resets native policy and fallback");
    f::check(p::install()&&p::native_clip_guard_attached(),"same-module second install begins with genuine guard");
    p::service_native_cursor_tail();
    f::check(!p::native_clip_guard_attached(),"second lifetime selects omission rather than old sticky fallback");
    p::uninstall();
    f::check(!p::is_installed()&&f::resets==2,"second complete teardown resets exactly once");
    std::printf("PASS %u actual cursor-tail lifecycle checks; controlled detour/native leaves, no game access\n",f::checks);
}
