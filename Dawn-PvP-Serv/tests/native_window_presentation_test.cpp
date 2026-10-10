#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace fixture {
std::array<unsigned char, 0x3000> tls{};
std::array<std::uintptr_t, 1> slots{};
unsigned __int64 gs(unsigned long offset) noexcept {
    assert(offset == 0x58);
    return reinterpret_cast<std::uintptr_t>(slots.data());
}
}
#define __readgsqword fixture::gs
#include "../dawn/src/client/hooks/graphics/native_window_presentation.cpp"
#undef __readgsqword
#include "../dawn/src/client/hooks/graphics/graphics_hook_lifecycle.cpp"
#include "../dawn/src/client/hooks/graphics/native_window_signatures.h"

namespace fixture {
using namespace dawn::client::hooks;
std::uintptr_t base{};
std::array<unsigned char, 0x100> pool{};
std::array<unsigned char, 0x1C80> parent{};
std::vector<int> events;
bool releaseSuccess = true;
bool renderFault{};
bool renderActive = true;
bool recursive{};
struct ComLifetime {
    void** table;
    unsigned references = 1;
};
ULONG STDMETHODCALLTYPE add_ref(ComLifetime* object) noexcept { return ++object->references; }
ULONG STDMETHODCALLTYPE release_ref(ComLifetime* object) noexcept { assert(object->references > 1); return --object->references; }
void* comTable[] = {nullptr, reinterpret_cast<void*>(&add_ref), reinterpret_cast<void*>(&release_ref)};
ComLifetime chain{comTable}, context{comTable};
bool inputRelease = true;
bool inputInstall = true;
bool rendererRelease = true;
bool recoveryRelease = true;
bool cursorRelease = true;
bool discoverySuccess = true;
bool detourSuccess = true;
unsigned attachCount{};
unsigned shutdowns{};
HWND window{};
void put(std::uintptr_t address, std::uintptr_t value) { std::memcpy(reinterpret_cast<void*>(address), &value, 8); }
void word(std::uintptr_t address, unsigned value) { std::memcpy(reinterpret_cast<void*>(address), &value, 4); }
std::uintptr_t acquire(std::uintptr_t suppliedPool, int mode) noexcept {
    assert(suppliedPool == reinterpret_cast<std::uintptr_t>(pool.data()) && mode == 0);
    assert(graphics::g_hookCallDepth && graphics::g_activeHookCalls.load());
    bool writerAcquired{};
    std::thread writer([&] { AcquireSRWLockExclusive(&graphics::g_hookLock); writerAcquired = true;
        ReleaseSRWLockExclusive(&graphics::g_hookLock); });
    writer.join(); assert(writerAcquired);
    events.push_back(1);
    word(suppliedPool + 0x10 + 0x14, 1);
    word(suppliedPool + 0x10 + 0x18, GetCurrentThreadId());
    const auto lease = suppliedPool + 0x10;
    put(reinterpret_cast<std::uintptr_t>(tls.data()) + 0x2558, lease);
    return lease;
}
bool release() noexcept {
    assert(graphics::native_window::thread_lease(base) == reinterpret_cast<std::uintptr_t>(pool.data()) + 0x10);
    events.push_back(3);
    if (releaseSuccess) {
        word(reinterpret_cast<std::uintptr_t>(pool.data()) + 0x10 + 0x14, 0);
        put(reinterpret_cast<std::uintptr_t>(tls.data()) + 0x2558, 0);
    }
    return releaseSuccess;
}
void select() {
    using namespace graphics;
    native_window::g_metadata = {base, &acquire, &release};
    native_window::g_window = window;
    native_window::g_selected = true;
    native_window::g_fallback = false;
    native_window::g_firstDraw = false;
    g_requiredHandles = 0;
    g_acceptRendererCalls = true;
    g_nativeRejected = false;
    g_fallbackPending = false;
    events.clear();
    word(base + 0x330F0E0, 0);
    put(base + 0x3CC77B0, reinterpret_cast<std::uintptr_t>(&GetCurrentThreadId));
    *reinterpret_cast<unsigned char*>(base + 0x1F89E04) = 1;
    word(base + 0x1FD0854, ~0U);
    word(base + 0x2814274, 1);
    put(base + 0x2817910, reinterpret_cast<std::uintptr_t>(pool.data()));
    put(base + 0x2817948, reinterpret_cast<std::uintptr_t>(&chain));
    put(reinterpret_cast<std::uintptr_t>(pool.data()) + 0x10 + 0x68, reinterpret_cast<std::uintptr_t>(&context));
    put(base + 0x26869C8, reinterpret_cast<std::uintptr_t>(window));
    put(reinterpret_cast<std::uintptr_t>(pool.data()) + 0x10, reinterpret_cast<std::uintptr_t>(parent.data()));
    word(reinterpret_cast<std::uintptr_t>(parent.data()) + 0x1C60, 7);
    word(reinterpret_cast<std::uintptr_t>(parent.data()) + 0x1C64, 7);
}
}

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace dawn::client::hooks::cursor {
bool prepare_native_tail_retirement() noexcept { return fixture::cursorRelease; }
}
namespace dawn::core::ui::runtime {
VisibilitySnapshot snapshot() noexcept { return {true, true, false, VK_INSERT}; }
}
namespace dawn::client::hooks::graphics::renderer {
bool prepare_native_frame(IDXGISwapChain* suppliedChain, ID3D11DeviceContext* suppliedContext) noexcept {
    using namespace fixture;
    assert(suppliedChain == reinterpret_cast<IDXGISwapChain*>(&chain));
    assert(suppliedContext == reinterpret_cast<ID3D11DeviceContext*>(&context));
    assert(!native_window::thread_lease(base) && native_window::in_preparation());
    assert(chain.references == 2 && context.references == 2); events.push_back(6);
    if (recursive) {
        recursive = false;
        // native 40D semantics: XCHG consumes ready before acquiring the nonrecursive owner, and
        // CPU prep has released its first lease, so nested forwarding can run
        const auto ready = InterlockedExchange(reinterpret_cast<volatile LONG*>(base + 0x2814274), 0);
        if (ready == 1) { (void)fixture::acquire(reinterpret_cast<std::uintptr_t>(pool.data()), 0);
            events.push_back(8); assert(fixture::release()); }
    }
    return renderActive;
}
bool submit_native_frame(IDXGISwapChain* suppliedChain, ID3D11DeviceContext* suppliedContext) noexcept {
    using namespace fixture;
    assert(suppliedChain == reinterpret_cast<IDXGISwapChain*>(&chain));
    assert(suppliedContext == reinterpret_cast<ID3D11DeviceContext*>(&context));
    assert(native_window::thread_lease(base) && !native_window::in_preparation());
    assert(chain.references == 2 && context.references == 2); events.push_back(2);
    if (renderFault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return true;
}
void finish_native_frame() noexcept {
    fixture::events.push_back(7);
}
bool active() noexcept { return fixture::renderActive; }
void check_native_device_loss() noexcept { fixture::events.push_back(5); }
bool service_pending_shutdown() noexcept { return fixture::recoveryRelease; }
bool shutdown() noexcept { ++fixture::shutdowns; return fixture::rendererRelease; }
void dispatch_pending_input_release() noexcept {}
}
namespace dawn::client::hooks::graphics::input {
bool install(HWND) noexcept { return fixture::inputInstall; }
bool uninstall() noexcept { return fixture::inputRelease; }
}
namespace dawn::client::hooks::graphics::discovery {
bool resolve(Targets& output) noexcept { if (!fixture::discoverySuccess) return false;
    output.present = reinterpret_cast<void*>(0x1234); output.resizeBuffers = reinterpret_cast<void*>(0x5678); return true; }
void release(Targets& output) noexcept { output = {}; }
}
namespace dawn::client::hooks::graphics::replacement {
EntryPoints entry_points() noexcept { return {reinterpret_cast<void*>(0x1111), reinterpret_cast<void*>(0x2222)}; }
ProtectedEntries protected_entries() noexcept { return {}; }
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs, std::span<Handle> output) noexcept {
    ++fixture::attachCount;
    if (!fixture::detourSuccess) return false;
    for (std::size_t i = 0; i < specs.size(); ++i) output[i] = {specs[i].target, specs[i].replacement, true};
    return true;
}
UninstallResult uninstall(std::span<Handle> handles, std::span<const ProtectedCodeEntry>) noexcept {
    for (auto& handle : handles) handle = {};
    return UninstallResult::removed;
}
}

int main() {
    using namespace fixture;
    using namespace dawn::client::hooks::graphics;
    base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, 0x3CD0000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(base);
    slots[0] = reinterpret_cast<std::uintptr_t>(tls.data());
    window = CreateWindowExW(0, L"STATIC", L"offline fixture", WS_OVERLAPPEDWINDOW, 0, 0, 20, 20, nullptr, nullptr, nullptr, nullptr);
    assert(window);
    using namespace native_window::signatures;
#define COPY_SIGNATURE(rva) std::memcpy(reinterpret_cast<void*>(base + 0x##rva), k##rva.data(), k##rva.size())
    COPY_SIGNATURE(1116D30); COPY_SIGNATURE(111E4E0); COPY_SIGNATURE(110BB59); COPY_SIGNATURE(3C5C70);
    COPY_SIGNATURE(122FA00); COPY_SIGNATURE(122FAC0); COPY_SIGNATURE(122FB50); COPY_SIGNATURE(12310F0);
    COPY_SIGNATURE(1231130); COPY_SIGNATURE(3C52B0);
    COPY_SIGNATURE(3C0400); COPY_SIGNATURE(3BFE15); COPY_SIGNATURE(3BFF48); COPY_SIGNATURE(111DF87); COPY_SIGNATURE(111E72F);
    COPY_SIGNATURE(355160);
    assert(matches(base));
    for (const auto rva : {0x1116D30,0x111E4E0,0x110BB59,0x3C5C70,0x122FA00,0x122FAC0,0x122FB50,0x12310F0,0x1231130,0x3C52B0,0x3C0400,0x3BFE15,0x3BFF48,0x111DF87,0x111E72F,0x355160}) {
        auto& byte = *reinterpret_cast<unsigned char*>(base + rva); byte ^= 1; assert(!matches(base)); byte ^= 1;
    }
    assert(!matches(0));
    assert(!is_installed());
    select(); assert(is_installed());
    SetLastError(123);
    { const native_window::FrameScope frame{window}; assert(GetLastError() == 123);
      assert(native_window::thread_lease(base) == 0); events.push_back(4);
      bool removed = true; std::thread remover([&] { removed = uninstall(); }); remover.join();
      assert(!removed && native_window::selected() && !g_acceptRendererCalls);
      SetLastError(456); }
    assert(GetLastError() == 456 && events == std::vector<int>({1,3,6,1,2,3,4,5,7}));
    assert(g_activeHookCalls.load() == 0 && g_hookCallDepth == 0);
    inputRelease = false; assert(!uninstall() && native_window::selected());
    inputRelease = true; assert(uninstall() && !is_installed());
    select(); recursive = true; { const native_window::FrameScope frame{window}; }
    assert(events == std::vector<int>({1,3,6,1,8,3,5,7})); assert(chain.references == 1 && context.references == 1); assert(uninstall());
    select(); word(base + 0x2814274, 0); { const native_window::FrameScope frame{window}; }
    assert(events == std::vector<int>({5,7}) && !native_window::fallback_requested()); assert(uninstall());
    select(); word(reinterpret_cast<std::uintptr_t>(parent.data()) + 0x1C64, 6);
    { const native_window::FrameScope frame{window}; }
    assert(events == std::vector<int>({1,3,5,7}) && native_window::fallback_requested());
    cursorRelease = false;
    const auto beforeCursorDrain = shutdowns;
    service_native_window();
    assert(native_window::selected() && attachCount == 0 && shutdowns == beforeCursorDrain);
    cursorRelease = true;
    rendererRelease = false; service_native_window(); assert(native_window::selected() && attachCount == 0);
    rendererRelease = true; inputRelease = false; service_native_window(); assert(native_window::selected() && attachCount == 0);
    inputRelease = true; detourSuccess = false; service_native_window();
    assert(!native_window::selected() && g_fallbackPending && !is_installed());
    detourSuccess = true; service_native_window(); assert(is_installed() && attachCount == 2 && !g_fallbackPending);
    assert(g_handles[0].attached && g_handles[1].attached && g_nativeRejected); assert(uninstall() && !g_nativeRejected);
    select(); word(base + 0x1FD0854, 0); { const native_window::FrameScope frame{window}; }
    assert(native_window::fallback_requested() && events == std::vector<int>({5,7})); assert(uninstall());
    select(); put(reinterpret_cast<std::uintptr_t>(tls.data()) + 0x2558, 0xFF);
    { const native_window::FrameScope frame{window}; }
    assert(native_window::fallback_requested() && events == std::vector<int>({5,7}));
    put(reinterpret_cast<std::uintptr_t>(tls.data()) + 0x2558, 0); assert(uninstall());
    select(); put(reinterpret_cast<std::uintptr_t>(pool.data()) + 0x10, 1);
    { const native_window::FrameScope frame{window}; } assert(native_window::fallback_requested() && native_window::thread_lease(base) == 0); assert(uninstall());
    select(); renderActive = false; { const native_window::FrameScope frame{window}; }
    renderActive = true; assert(native_window::fallback_requested()); assert(uninstall());
    select(); releaseSuccess = false; { const native_window::FrameScope frame{window}; }
    releaseSuccess = true; assert(native_window::fallback_requested());
    assert(native_window::thread_lease(base) != 0);
    put(reinterpret_cast<std::uintptr_t>(tls.data()) + 0x2558, 0); assert(uninstall());
    select(); native_window::g_window = nullptr; put(base + 0x2817910, 0);
    assert(native_window::service_bootstrap() && native_window::selected()); assert(uninstall());
    assert(DestroyWindow(window)); assert(VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE));
    std::puts("PASS: exact text fences; zero-slot state; pin/release/CPU/acquire/GPU/release/forward ordering; genuine ready-consuming recursive presentation; active-call and retired-binding retention; real detour fallback/retry; native mode/queue/TLS rejection; SEH cleanup; default startup retry.");
}
