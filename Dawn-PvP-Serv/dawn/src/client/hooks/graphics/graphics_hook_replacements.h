#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <d3d11.h>
#include <dxgi.h>

#include "../../hooking/detour.h"
#include "input/input.h"
#include "renderer/renderer.h"

namespace dawn::client::hooks::graphics {

using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers =
    HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
enum class HookSlot : std::size_t {
    present,
    resizeBuffers,
    count,
};

inline constexpr std::size_t kHandleCount = static_cast<std::size_t>(HookSlot::count);
// vtable index: IDXGISwapChain inherits 7 methods before Present
inline constexpr std::size_t kPresentMethodIndex = 8;
// vtable index: ResizeBuffers is 5 methods after Present
inline constexpr std::size_t kResizeBuffersMethodIndex = 13;

struct Targets {
    void* present{};
    void* resizeBuffers{};
    HMODULE d3d11Module{};
    HMODULE dxgiModule{};
};

extern SRWLOCK g_hookLock;
extern std::atomic_uint g_activeHookCalls;
// per-thread depth stops an SRW upgrade from inside an original SDK call chain
extern thread_local std::size_t g_hookCallDepth;
extern bool g_acceptRendererCalls;

[[nodiscard]] inline bool hook_calls_idle() noexcept {
    return g_activeHookCalls.load(std::memory_order_acquire) == 0;
}

[[nodiscard]] inline bool current_thread_in_hook_call() noexcept {
    return g_hookCallDepth != 0;
}

extern std::array<hooking::detour::Handle, kHandleCount> g_handles;
extern std::array<void*, kHandleCount> g_targetEntries;
extern Targets g_targets;

[[nodiscard]] bool executable_image_target(const void* target, HMODULE module) noexcept;

template <typename Function> [[nodiscard]] Function original(HookSlot slot) noexcept {
    const std::size_t index = static_cast<std::size_t>(slot);
    void* entry = g_handles[index].original;
    if (entry == nullptr) {
        entry = g_targetEntries[index];
    }
    return reinterpret_cast<Function>(entry);
}

namespace discovery {

[[nodiscard]] bool resolve(Targets& output) noexcept;

void release(Targets& targets) noexcept;

} // namespace discovery

namespace replacement {

inline constexpr std::size_t kProtectedEntryCount = 4;

using EntryPoints = std::array<void*, kHandleCount>;
using ProtectedEntries = std::array<hooking::detour::ProtectedCodeEntry, kProtectedEntryCount>;

[[nodiscard]] EntryPoints entry_points() noexcept;

[[nodiscard]] ProtectedEntries protected_entries() noexcept;

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swapChain,
                                  UINT syncInterval,
                                  UINT flags) noexcept;

} // namespace replacement

} // namespace dawn::client::hooks::graphics
