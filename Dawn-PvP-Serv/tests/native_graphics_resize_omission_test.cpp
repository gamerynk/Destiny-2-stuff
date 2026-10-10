#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace fixture {
std::vector<char> nativeText;
std::uintptr_t corruptRva{};
unsigned checks{}, installs{}, selectedCount{}, releases{}, removals{};
bool attachOk{true}, shutdownOk{true}, detachOk{true};
void require(bool result,const char* reason) {
    ++checks;
    if(!result){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}
}
BOOL WINAPI read_native(HANDLE, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* copied) {
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto rva=reinterpret_cast<std::uintptr_t>(address)-base;
    if(rva<0x1000 || rva-0x1000+size>nativeText.size())return FALSE;
    std::memcpy(output,nativeText.data()+rva-0x1000,size);
    if(rva==corruptRva && size!=0)static_cast<unsigned char*>(output)[0]^=1;
    *copied=size;return TRUE;
}
}
#define ReadProcessMemory fixture::read_native
#define RESTORATION_NATIVE_RESIZE_OMISSION 1
#define RESTORATION_TRANSIENT_RTV 1
#include "../dawn/src/client/hooks/graphics/graphics_hook_lifecycle.cpp"
#undef ReadProcessMemory

namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::client::hooks::cursor {
bool prepare_native_tail_retirement() noexcept { return true; }
}
namespace dawn::core::ui::runtime {
VisibilitySnapshot snapshot() noexcept{return {true,true,false,VK_INSERT};}
}
namespace dawn::client::hooks::graphics {
namespace discovery {
bool resolve(Targets& out) noexcept{out.present=reinterpret_cast<void*>(1);out.resizeBuffers=reinterpret_cast<void*>(2);return true;}
void release(Targets& targets) noexcept{targets={};++fixture::releases;}
}
namespace native_window {
bool resolve() noexcept{return false;}
bool selected() noexcept{return false;}
bool fallback_requested() noexcept{return false;}
bool service_bootstrap() noexcept{return true;}
bool shutdown() noexcept{return true;}
}
namespace renderer {
bool service_pending_shutdown() noexcept{return true;}
bool shutdown() noexcept{return fixture::shutdownOk;}
void dispatch_pending_input_release() noexcept{}
}
namespace replacement {
EntryPoints entry_points() noexcept{return {reinterpret_cast<void*>(3),reinterpret_cast<void*>(4)};}
ProtectedEntries protected_entries() noexcept{return {};}
}
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs,std::span<Handle> handles) noexcept {
    ++fixture::installs;fixture::selectedCount=static_cast<unsigned>(specs.size());
    fixture::require(specs.size()==handles.size(),"selected graphics specs and handles align");
    if(!fixture::attachOk)return false;
    for(std::size_t i=0;i<specs.size();++i)handles[i]={specs[i].target,specs[i].replacement,true};
    return true;
}
UninstallResult uninstall(std::span<Handle> handles,std::span<const ProtectedCodeEntry>) noexcept {
    ++fixture::removals;if(!fixture::detachOk)return UninstallResult::failed;
    for(auto& handle:handles)handle={};return UninstallResult::removed;
}
}

int main(int argc,char** argv){
    namespace g=dawn::client::hooks::graphics;
    fixture::require(argc==2,"captured native text fixture supplied");
    std::ifstream input(argv[1],std::ios::binary);
    fixture::nativeText.assign(std::istreambuf_iterator<char>(input),{});
    fixture::require(fixture::nativeText.size()==28898816,"native text fixture has exact supported extent");
    fixture::require(g::install()&&g::is_installed()&&fixture::selectedCount==1,
        "validated native owner installs only Present");
    fixture::require(g::g_handles[0].attached&&!g::g_handles[1].attached,
        "ResizeBuffers handle physically absent in native-owner variant");
    const auto installs=fixture::installs;
    fixture::require(g::install()&&fixture::installs==installs,"repeat install recognizes one-hook surface");
    fixture::shutdownOk=false;
    fixture::require(!g::uninstall()&&g::is_installed()&&g::g_acceptRendererCalls,
        "pending renderer cleanup retains attached owner Present");
    fixture::shutdownOk=true;fixture::detachOk=false;
    fixture::require(!g::uninstall()&&g::is_installed()&&g::g_targets.present!=nullptr,
        "failed detach retains original forwarding entries");
    fixture::detachOk=true;
    fixture::require(g::uninstall()&&!g::is_installed()&&!g::g_handles[0].attached
        &&g::g_targets.present==nullptr,"retry clears omitted and attached slots after successful cleanup");
    for(const auto rva:{0x3C0400u,0x3BFE15u,0x3BFF48u,0x111DF87u,0x111E72Fu}){
        fixture::corruptRva=rva;
        fixture::require(g::install()&&fixture::selectedCount==2&&g::is_installed(),
            "each unsupported owner/call-site contract falls back to both graphics hooks");
        fixture::require(g::g_handles[0].attached&&g::g_handles[1].attached,
            "fallback preserves resize lifetime interception");
        fixture::require(g::uninstall(),"fallback cleanup succeeds");
    }
    fixture::corruptRva=0;fixture::attachOk=false;
    fixture::require(!g::install()&&!g::is_installed()&&g::g_targets.present==nullptr,
        "failed native-only attachment frees resolved targets");
    fixture::require(g::uninstall(),"failed attach leaves clean teardown state");
    std::printf("PASS %u native graphics resize omission lifecycle checks; no game access\n",fixture::checks);
}
