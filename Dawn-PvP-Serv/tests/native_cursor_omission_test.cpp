#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../dawn/src/client/hooks/cursor/cursor_hook_lifecycle.cpp"
namespace f {
unsigned checks{},attached{},policyEdges{};
bool ready{},failAttach{},failDetach{};
std::string lastLog;
void require(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
}
namespace dawn::core::log {void write(Channel,Level,std::string_view text) noexcept{f::lastLog=text;}}
namespace dawn::client::hooks::polled_input {bool native_pause_available() noexcept{return f::ready;}}
namespace dawn::client::hooks::cursor {
std::array<hooking::detour::Handle,kHandleCount> g_handles{};
std::array<void*,kHandleCount> g_targets{};
BOOL WINAPI set_cursor_pos(int,int) noexcept{return TRUE;}
BOOL WINAPI clip_cursor(const RECT*) noexcept{return TRUE;}
void apply_policy(bool visible) noexcept{if(!visible)++f::policyEdges;}
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> handles) noexcept{
    if(f::failAttach)return false;
    f::attached=static_cast<unsigned>(specs.size());
    for(std::size_t i=0;i<handles.size();++i)handles[i]={specs[i].target,specs[i].replacement,true};
    return true;
}
bool uninstall(std::span<Handle> handles) noexcept{
    if(f::failDetach)return false;
    for(auto& h:handles)h={};return true;
}
}
int main(){
    namespace p=dawn::client::hooks::cursor;
    f::ready=true;
    const unsigned count=RESTORATION_NATIVE_SET_CURSOR_OMISSION?1u:2u;
    f::require(p::install()&&p::is_installed()&&f::attached==count,"actual transaction selects physical cursor count");
    f::require(p::g_handles[1].attached&&p::g_handles[0].attached==(count==2),"confinement guard always retained in stable slot");
    f::require(p::g_handles[1].original==p::g_targets[1],"selected confinement spec matches its original export");
    f::require(p::g_targets[0]&&p::original<p::SetCursorPos>(p::HookSlot::setCursorPos),
        "omitted move remains callable as genuine restored System32 export");
    f::require(p::install()&&f::attached==count,"repeat install preserves selection");
    f::failDetach=true;p::uninstall();
    f::require(p::is_installed()&&p::g_handles[1].attached&&p::g_targets[1]&&p::g_module,
        "failed detach retains active confinement handle/export/module");
    f::require(f::policyEdges==1,"game confinement restoration requested before detach attempt");
    f::failDetach=false;p::uninstall();
    f::require(!p::is_installed()&&!p::g_handles[1].attached&&!p::g_targets[0]&&!p::g_module,
        "successful detach releases both exports/module");
    f::ready=false;
    f::require(p::install()&&f::attached==2&&p::g_handles[0].attached,"unavailable native contract retains both guards");
    p::uninstall();f::failAttach=true;
    f::require(!p::install()&&!p::is_installed()&&!p::g_targets[1]&&!p::g_module,
        "failed selected attach releases targets/module without claiming install");
    std::printf("PASS %u actual cursor omission lifetime checks; option=%u; no game or cursor writes\n",
        f::checks,RESTORATION_NATIVE_SET_CURSOR_OMISSION);
}
