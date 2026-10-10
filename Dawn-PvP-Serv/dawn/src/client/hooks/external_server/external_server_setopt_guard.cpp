#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../network/content_config/runtime.h"
#include "../network/network_call_quiescence.h"
#include "internal.h"
#include "route.h"
#include "url_diagnostic.h"

namespace dawn::client::hooks::external_server {
namespace {
using Setopt = int (*)(void*, int, void*) noexcept;
hooking::detour::Handle g_handle{};
SRWLOCK g_lifecycleLock{SRWLOCK_INIT};
Setopt g_original{};
bool g_accepting{};
volatile LONG g_activeCalls{};
constexpr DWORD kDetachTimeoutMilliseconds = 3000;
constexpr int kFailedInit = 2;
constexpr int kMalformedUrl = 3;

struct Lease final {Setopt original{}; bool accepting{};};

// a thread suspended before the increment must block detach too
__declspec(noinline) Lease ingress() noexcept {
    (void)InterlockedIncrement(&g_activeCalls);
    AcquireSRWLockShared(&g_lifecycleLock);
    const Lease lease{g_original, g_accepting};
    ReleaseSRWLockShared(&g_lifecycleLock);
    return lease;
}

__declspec(noinline) void egress() noexcept {
    WakeByAddressAll(const_cast<LONG*>(&g_activeCalls));
    (void)InterlockedDecrement(&g_activeCalls);
}

bool idle() noexcept {return network::quiescence::active_calls(g_activeCalls) == 0;}

bool http_scheme(std::string_view url) noexcept {
    const auto scheme = url.substr(0, url.find("://"));
    if (scheme.size() != 4 && scheme.size() != 5) {
        return false;
    }
    constexpr std::string_view expected = "https";
    for (std::size_t index = 0; index < scheme.size(); ++index) {
        if ((static_cast<unsigned char>(scheme[index]) | 0x20U)
            != static_cast<unsigned char>(expected[index])) {
            return false;
        }
    }
    return url.find("://") != std::string_view::npos;
}

DWORD main_image_size(HMODULE module) noexcept {
    if (module == nullptr) return 0;
    __try {
        const auto* base = reinterpret_cast<const std::byte*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0
            || dos->e_lfanew > 4096) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE
            || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
        return nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void report_url(std::string_view from, std::string_view to) noexcept {
    static volatile LONG reported{};
    if (InterlockedIncrement(&reported) > 32) return;
    from = diagnostic::safe_url(from);
    to = diagnostic::safe_url(to);
    std::array<void*, 16> frames{};
    const auto count = CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()),
                                             frames.data(), nullptr);
    const auto module = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto imageSize = main_image_size(module);
    std::array<char, 128> callers{};
    std::size_t used{};
    unsigned retained{};
    for (USHORT index = 0; index < count && retained < 6; ++index) {
        std::uint32_t rva{};
        if (!diagnostic::main_image_rva(base, imageSize,
                reinterpret_cast<std::uintptr_t>(frames[index]), rva)) continue;
        const int length = std::snprintf(callers.data() + used, callers.size() - used,
            "%s%08X", retained == 0 ? "" : ",", static_cast<unsigned>(rva));
        if (length <= 0 || static_cast<std::size_t>(length) >= callers.size() - used) break;
        used += static_cast<std::size_t>(length);
        ++retained;
    }
    std::array<char, 2 * kUrlCapacity + 192> line{};
    const int written = std::snprintf(line.data(), line.size(),
        "ev=external stage=setopt_url from=%.*s to=%.*s result=%s native_callers=%s",
        static_cast<int>(from.size()), from.data(), static_cast<int>(to.size()), to.data(),
        to.empty() ? "blocked" : "ok", retained == 0 ? "unavailable" : callers.data());
    if (written > 0 && static_cast<std::size_t>(written) < line.size()) {
        core::log::write(core::log::Channel::client, core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

int route(const Lease& lease, void* handle, int option, void* value) noexcept {
    if (lease.original == nullptr) {
        return kFailedInit;
    }
    // every other option, including both TLS verify options, passes through unchanged
    if (option != kUrl) {
        return lease.original(handle, option, value);
    }
    if (!lease.accepting || !network::content_config::prepare_native_request()) {
        return kFailedInit;
    }
    if (value == nullptr) {
        return kMalformedUrl;
    }
    const auto* text = static_cast<const char*>(value);
    const std::size_t length = strnlen_s(text, kUrlCapacity);
    const std::string_view original(text, length);
    if (length == kUrlCapacity || original.find_first_of("\r\n") != std::string_view::npos
        || !http_scheme(original)) {
        return kMalformedUrl;
    }
    const std::size_t authorityStart = original.find("://") + 3;
    const std::size_t authorityEnd = original.find_first_of("/?#", authorityStart);
    if (authorityStart == length || authorityEnd == authorityStart) {
        return kMalformedUrl;
    }
    // CURLOPT_URL copies its argument before returning
    thread_local std::array<char, kUrlCapacity> held{};
    const std::size_t size = rewrite_host(original, held);
    report_url(original, {held.data(), size});
    if (size == 0) {
        return kMalformedUrl; // a failed rewrite must never expose the retail origin
    }
    return lease.original(handle, option, held.data());
}

__declspec(noinline) int setopt(void* handle, int option, void* value) noexcept {
    const Lease lease = ingress();
    int result = kFailedInit;
    __try {
        result = route(lease, handle, option, value);
    } __finally {
        egress();
    }
    return result;
}

bool fail_install(const char* reason) noexcept {
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(), line.size(),
        "ev=external stage=install result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}
}

bool install() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!enabled() || g_handle.attached) {
        const bool installed = enabled() && g_original != nullptr && g_accepting;
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return installed;
    }
    std::byte* target = scan_main_image_unique(kSetoptSignature, "curl_easy_setopt");
    bool installed = false;
    if (target != nullptr) {
        installed = hooking::detour::install({target, reinterpret_cast<void*>(&setopt)}, g_handle);
    }
    // new calls wait in ingress until original and admission are published
    if (installed) {
        g_original = reinterpret_cast<Setopt>(g_handle.original);
        g_accepting = true;
    }
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    if (!installed) {
        return fail_install(target == nullptr ? "target" : "attach");
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
                     "ev=external stage=install result=ok");
    return true;
}

bool uninstall() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!g_handle.attached) {
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return true;
    }
    g_accepting = false;
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    const bool drained = network::quiescence::wait_for_idle(g_activeCalls, kDetachTimeoutMilliseconds);
    AcquireSRWLockExclusive(&g_lifecycleLock);
    const std::array protectedEntries{
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&setopt)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&ingress)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&egress)},
    };
    const bool removed = drained && hooking::detour::uninstall(g_handle, protectedEntries, &idle)
                                     == hooking::detour::UninstallResult::removed;
    if (removed) {
        g_original = nullptr;
    } else {
        g_accepting = true; // keep the live trampoline and gate on failed detach
    }
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return removed;
}

bool is_installed() noexcept {
    AcquireSRWLockShared(&g_lifecycleLock);
    const bool installed = g_handle.attached && g_original != nullptr && g_accepting;
    ReleaseSRWLockShared(&g_lifecycleLock);
    return installed;
}

bool has_ownership() noexcept {
    AcquireSRWLockShared(&g_lifecycleLock);
    const bool owned = g_handle.attached;
    ReleaseSRWLockShared(&g_lifecycleLock);
    return owned;
}
}
