#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string_view>
#include "../dawn/src/client/hooks/cursor/native_cursor_tail_runtime.h"
#include "../dawn/src/client/hooks/cursor/native_cursor_pending_tail.h"
#include "../dawn/src/client/diagnostics/module_range.h"
#include "../dawn/src/core/logging/log.h"
#include "../dawn/src/core/ui/runtime/ui_visibility_runtime.h"
namespace fixture {
std::uintptr_t image{};
bool classValid{true}, safe{true}, paused{}, preparation{}, guard{}, recovery{}, modal{};
unsigned callbacks{}, clears{}, releases{}, clipCalls{}, restores{}, logs{};
std::uint64_t sequence{70};
dawn::client::hooks::graphics::input::MainInputSnapshot main{};
dawn::client::hooks::graphics::input::RawOpeningLease lease{};
LONG_PTR WINAPI class_pointer(HWND, int) { return classValid ? static_cast<LONG_PTR>(image + 0x3C0400) : 0; }
}
namespace dawn::client::diagnostics { bool module_range(HMODULE, ModuleRange&) noexcept { return false; } }
namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept { ++fixture::logs; } }
namespace dawn::client::hooks::graphics::input {
MainInputSnapshot snapshot_main_input() noexcept { return fixture::main; }
RawInputSnapshot snapshot_raw_input() noexcept {
 RawInputSnapshot s{}; s.installed=true; s.bindingGeneration=50; return s;
}
bool claim_raw_opening(std::uint64_t generation, std::uint64_t binding, RawOpeningLease& l) noexcept {
 if(fixture::lease.identity) return false;
 l={++fixture::sequence,generation,binding,reinterpret_cast<HWND>(std::uintptr_t{60})};
 fixture::lease=l; return true;
}
RawOpeningSnapshot snapshot_raw_opening(const RawOpeningLease& l) noexcept {
 RawOpeningSnapshot s{};s.lease=fixture::lease;s.activeCallbacks=fixture::callbacks;
 s.matched=l.identity && l==fixture::lease;s.drained=s.matched&&!s.activeCallbacks;return s;
}
bool release_raw_opening(const RawOpeningLease& l) noexcept {
 if(!l.identity || l!=fixture::lease)return false;
 fixture::lease={};++fixture::releases;return true;
}
}
namespace dawn::client::hooks::graphics::native_window { bool in_preparation() noexcept { return fixture::preparation; } }
namespace dawn::client::hooks::polled_input {
bool native_pause_owned() noexcept { return fixture::paused; }
bool native_pause_available() noexcept { return true; }
bool native_key_recovery_safe() noexcept { return fixture::safe; }
void apply_visibility(bool v) noexcept { fixture::paused=v; }
}
namespace dawn::client::hooks::cursor {
void request_native_clip_guard() noexcept { fixture::recovery=true; }
bool native_clip_guard_attached() noexcept { return fixture::guard; }
void publish_guarded_visibility(bool v) noexcept { fixture::modal=v; }
void restore_guarded_policy(bool v) noexcept { fixture::modal=v; ++fixture::restores; }
}
#define GetClassLongPtrW fixture::class_pointer
#include "../dawn/src/client/hooks/cursor/native_cursor_tail_runtime.cpp"
#undef GetClassLongPtrW
namespace {
namespace t=dawn::client::hooks::cursor::tail;
namespace p=dawn::client::hooks::cursor::pending_tail;
namespace ui=dawn::core::ui::runtime;
unsigned checks{};
void check(bool value,const char* reason) { ++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);} }
bool clear_leaf(void*) noexcept { ++fixture::clears;return true; }
bool clip_leaf(void*,const RECT*) noexcept { ++fixture::clipCalls;return true; }
bool geometry_leaf(void*,HWND,RECT& rectangle) noexcept { rectangle={1,2,801,602};return true; }
void configure() {
 t::g_image.store(fixture::image);
 p::Bindings b{nullptr,t::read_state,t::read_basic,t::claim,t::raw,t::release,t::raw_binding,
   clear_leaf,t::open,t::close,t::apply,geometry_leaf,clip_leaf,t::fallback,
   t::guarded_visibility,t::guard_attached};
 check(t::g_policy.configure(true,b),"actual adapter policy configured with controlled native leaves");
 t::g_used.store(true);t::g_retired.store(false);
}
void owner_tail() { ++fixture::main.revision;t::OwnerScope scope{fixture::main.window,WM_USER+13}; }
void open() {
 check(t::request_toggle(fixture::main.window,VK_INSERT),"actual request route consumes key");
 check(!ui::snapshot().visible,"key request cannot directly flip Core");
 owner_tail();check(ui::snapshot().visible&&fixture::paused,"owner commits actual Core then pause");
}
}
int main() {
 fixture::image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x2700000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 check(fixture::image!=0,"allocate fixture scalar image only");
 HWND window=CreateWindowExW(0,L"STATIC",L"cursor adapter fixture",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 check(window!=nullptr,"own hidden HWND");
 fixture::main={window,GetCurrentThreadId(),20,1,true};
 *reinterpret_cast<HWND*>(fixture::image+0x26869C8)=window;
 *reinterpret_cast<unsigned*>(fixture::image+0x2686998)=0;
 *reinterpret_cast<unsigned*>(fixture::image+0x24218E0)=1;
 check(ui::initialize({true,VK_INSERT}),"actual Core initialize");
 configure();fixture::callbacks=1;
 check(t::request_toggle(window,VK_INSERT),"pending actual route request");owner_tail();
 check(!ui::snapshot().visible&&fixture::lease.identity&&fixture::clears==0,"actual adapter waits old raw count while hidden");
 fixture::callbacks=0;owner_tail();
 check(ui::snapshot().visible&&fixture::paused&&fixture::modal&&t::omission_ready(),"actual owner opens after drain and publishes retained real-guard modal answer");
 check(t::request_toggle(window,VK_INSERT),"normal close request");owner_tail();
 check(!ui::snapshot().visible&&!fixture::lease.identity&&!fixture::paused&&!fixture::modal,"actual close restores/releases with retained guard no longer suppressing rectangle");
 fixture::callbacks=1;check(t::request_toggle(window,VK_INSERT),"fallback pending request");owner_tail();
 fixture::classValid=false;owner_tail();
 check(fixture::recovery&&!t::omission_ready()&&t::configured(),"failed observation cannot republish readiness");
 owner_tail();check(t::configured()&&fixture::lease.identity,"requested guard is not attached permission");
 fixture::guard=true;owner_tail();
 check(t::configured()&&fixture::lease.identity,"actual guard still waits admitted old callbacks");
 fixture::callbacks=0;owner_tail();
 check(!t::configured()&&!fixture::lease.identity&&ui::snapshot().visible,"guarded handoff finishes despite persistent classifier loss");
 check(fixture::modal&&fixture::paused&&fixture::restores==1,"legacy modal policy published and restored");
 check(!t::request_toggle(window,VK_INSERT),"retired adapter returns future key to actual Core path");
 check(ui::toggle_for_key(VK_INSERT)&&!ui::snapshot().visible,"legacy toggle is usable after finite handoff");
 check(t::prepare_shutdown(),"guarded retired state permits lifecycle cleanup");
 t::reset_after_uninstall();
 check(!t::manages_policy()&&!t::g_stopping.load()&&!t::g_rejected.load()&&!t::g_image.load(),"full teardown clears latched state");
 fixture::classValid=true;fixture::recovery=false;fixture::guard=false;fixture::paused=false;
 configure();open();
 check(!t::prepare_shutdown(),"visible second install signals rather than retiring bindings");
 check(ui::snapshot().visible&&fixture::lease.identity,"shutdown signal performs no native/Core mutation");
 owner_tail();
 check(!ui::snapshot().visible&&!fixture::lease.identity&&!t::configured(),"actual owner performs second-install close/drain/shutdown");
 check(t::prepare_shutdown(),"second shutdown retry succeeds");t::reset_after_uninstall();
 fixture::paused=false;configure();open();
 check(!t::prepare_retirement(),"visible renderer retirement waits owner");owner_tail();
 check(t::prepare_retirement()&&!ui::snapshot().visible&&!fixture::lease.identity,"retirement finishes before subclasses disappear");
 check(t::g_reopen.load(),"intended reopen retained during renderer replacement");
 ++fixture::main.bindingGeneration;t::binding_started();owner_tail();
 check(ui::snapshot().visible&&fixture::lease.identity&&fixture::paused,"new binding uses fresh pre-visible open after suspended cleanup");
 check(!t::prepare_shutdown(),"final visible shutdown signals");owner_tail();
 check(t::prepare_shutdown(),"final owner shutdown finishes");t::reset_after_uninstall();
 ui::shutdown();DestroyWindow(window);VirtualFree(reinterpret_cast<void*>(fixture::image),0,MEM_RELEASE);
 std::printf("PASS %u actual cursor-tail adapter/Core checks; controlled native/raw leaves, no game access\n",checks);
}
