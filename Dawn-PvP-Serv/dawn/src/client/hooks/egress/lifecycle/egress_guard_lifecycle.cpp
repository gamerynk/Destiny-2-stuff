#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../internal.h"
#include "../platform/abi.h"
#include "../runtime.h"
#include "internal.h"

namespace dawn::client::hooks::egress {

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<hooking::detour::Handle, kHookCount> g_handles{};

namespace {

namespace lifecycle = lifecycle;

std::array<const char*, kHookCount> g_exportNames{};
std::array<bool, kHookCount> g_exportResolved{};
bool g_batchAttached{};
// two exports can reach the report; only the first emits it
bool g_reported{};
HMODULE g_ownerModule{};
std::size_t g_activeHookCount{};

[[nodiscard]] bool all_installed() noexcept {
    if (g_activeHookCount < lifecycle::kRequiredHookCount || g_activeHookCount > g_handles.size()) {
        return false;
    }
    for (std::size_t index = 0; index < g_activeHookCount; ++index) {
        if (!g_handles[index].attached) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool any_installed() noexcept {
    return std::any_of(g_handles.begin(),
                       g_handles.end(),
                       [](const hooking::detour::Handle& handle) { return handle.attached; });
}

// pin the image: process-lifetime hooks survive Steam shutdown
[[nodiscard]] bool pin_owner_module() noexcept {
    if (g_ownerModule != nullptr) {
        return true;
    }
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
    return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&install), &g_ownerModule) != FALSE;
}

} // namespace

bool install() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (all_installed()) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (any_installed() || !pin_owner_module() || !lifecycle::load_modules()) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    std::array<hooking::detour::Spec, kHookCount> specs{};
    std::size_t count = 0;
    if (!lifecycle::resolve_specs(specs, g_exportNames, g_exportResolved, count)
        || !hooking::detour::install(std::span(specs.data(), count),
                                     std::span(g_handles.data(), count))) {
        g_activeHookCount = 0;
        g_batchAttached = false;
        lifecycle::release_modules();
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_activeHookCount = count;
    g_batchAttached = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void report_installation() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_reported) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    g_reported = true;
    const std::size_t count = g_activeHookCount;
    const bool attached = g_batchAttached;
    for (std::size_t index = 0; index < kHookCount; ++index) {
        if (g_exportNames[index] == nullptr) {
            continue;
        }
        const bool ready = g_exportResolved[index] && index < count && attached;
        std::array<char, 128> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=hook stage=attach group=egress name=%s result=%s",
                                          g_exportNames[index],
                                          ready ? "ok" : "fail");
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             ready ? core::log::Level::debug : core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    std::array<char, 96> summary{};
    const int written = std::snprintf(summary.data(),
                                      summary.size(),
                                      "ev=hook stage=install group=egress count=%zu result=%s",
                                      count,
                                      attached ? "ok" : "fail");
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         attached ? core::log::Level::info : core::log::Level::error,
                         {summary.data(), static_cast<std::size_t>(written)});
    }
}

bool is_installed() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool installed = all_installed();
    ReleaseSRWLockShared(&g_lock);
    return installed;
}

} // namespace dawn::client::hooks::egress
