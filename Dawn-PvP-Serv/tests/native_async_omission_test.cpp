#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../dawn/src/client/hooks/polled_input/polled_input_lifecycle.cpp"

namespace f {
unsigned checks{},attached{},clears{};
bool ready{},rangeOk{true},failAttach{},failDetach{};
std::string lastLog;
void require(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
}
namespace dawn::core::log {
void write(Channel,Level,std::string_view line) noexcept { f::lastLog=line; }
}
namespace dawn::client::hooks::polled_input {
std::array<hooking::detour::Handle,kHandleCount> g_handles{};
std::array<void*,kHandleCount> g_targets{};
bool native_pause_available() noexcept { return f::ready; }
bool native_key_focus_available() noexcept { return false; }
bool native_key_recovery_safe() noexcept { return true; }
bool resolve_game_range() noexcept { return f::rangeOk; }
void clear_game_range() noexcept { ++f::clears; }
bool drain_native_key_state() noexcept { return true; }
void apply_policy(bool) noexcept {}
SHORT WINAPI get_key_state(int) noexcept { return 0; }
SHORT WINAPI get_async_key_state(int) noexcept { return 0; }
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> handles) noexcept {
    if(f::failAttach)return false;
    f::attached=static_cast<unsigned>(specs.size());
    for(std::size_t i=0;i<handles.size();++i)handles[i]={specs[i].target,specs[i].replacement,true};
    return true;
}
bool uninstall(std::span<Handle> handles) noexcept {
    if(f::failDetach)return false;
    for(auto& h:handles)h={};return true;
}
}
int main() {
    namespace p=dawn::client::hooks::polled_input;
    f::ready=true;
    const unsigned expected=RESTORATION_NATIVE_ASYNC_OMISSION ? 1u : 2u;
    f::require(p::install()&&f::attached==expected,"selected transaction has physical expected count");
    f::require(p::g_handles[0].attached&&p::g_handles[1].attached==(expected==2),"synchronous guard always remains; omitted async handle empty");
    f::require(p::g_targets[1]!=nullptr,"omitted export remains resolved for clean native passthrough");
    f::require(p::install()&&f::attached==expected,"repeat install preserves selected transaction");
    f::failDetach=true;p::uninstall();
    f::require(p::is_installed()&&p::g_handles[0].attached&&p::g_targets[0]!=nullptr,"failed detach retains active guards, targets and module");
    f::failDetach=false;p::uninstall();
    f::require(!p::is_installed()&&!p::g_handles[0].attached&&p::g_targets[0]==nullptr,"successful teardown clears selected transaction");
    f::ready=false;
    f::require(p::install()&&f::attached==2&&p::g_handles[1].attached,"unavailable native pause falls back to both guards");
    p::uninstall();f::failAttach=true;
    f::require(!p::install()&&!p::is_installed()&&p::g_targets[0]==nullptr,"attach failure releases targets without partial selection");
    f::failAttach=false;f::rangeOk=false;
    f::require(!p::install()&&!p::is_installed(),"invalid main image fails before native omission admission");
    std::printf("PASS %u actual async omission lifecycle checks; option=%u; no game writes\n",f::checks,RESTORATION_NATIVE_ASYNC_OMISSION);
}
