#include "egress_policy_logging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../../../core/logging/log.h"

namespace dawn::client::hooks::egress::policy {
namespace {

constexpr std::string_view kConnectDenyRedirect =
    "ev=egress stage=connect target=redirect result=deny";
constexpr std::string_view kConnectDenyOther = "ev=egress stage=connect target=other result=deny";
constexpr std::string_view kSendDenyRedirect = "ev=egress stage=send target=redirect result=deny";
constexpr std::string_view kSendDenyOther = "ev=egress stage=send target=other result=deny";
constexpr std::string_view kNativeSdrAllow =
    "ev=egress stage=native_sdr target=valve_sdr action=allow result=ok";
constexpr std::size_t kNameEventCapacity = 448;
constexpr std::size_t kNameLimit = 255;
// relay probes are frequent, so log their policy decision once per process
std::atomic_bool g_nativeSdrAllowReported{false};
struct NativeSdrCounts final {
    std::atomic_uint32_t calls{};
    std::atomic_uint64_t bytes{};
    std::atomic_uint32_t immediate{};
    std::atomic_uint32_t pending{};
    std::atomic_uint32_t wouldBlock{};
    std::atomic_uint32_t errors{};
};
std::array<NativeSdrCounts, static_cast<std::size_t>(NativeSdrPath::count) * 2U>
    g_nativeSdrCounts{};
constexpr std::array<const char*, static_cast<std::size_t>(NativeSdrPath::count)>
    kNativeSdrPathNames{"connect", "send", "WSASend", "sendto",
                        "WSASendTo", "WSASendMsg", "recvfrom", "WSARecvFrom"};

struct CallerSite final {
    std::array<char, 32> module{};
    std::uintptr_t offset{};
};

[[nodiscard]] CallerSite inspect_caller(const void* caller) noexcept {
    CallerSite site{};
    HMODULE owner{};
    if (caller == nullptr
        || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(caller),
                              &owner)
               == FALSE) {
        return site;
    }
    site.offset =
        reinterpret_cast<std::uintptr_t>(caller) - reinterpret_cast<std::uintptr_t>(owner);
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()));
    std::size_t start = 0;
    for (DWORD index = 0; index < length; ++index) {
        if (path[index] == L'\\' || path[index] == L'/') {
            start = index + 1;
        }
    }
    std::size_t written = 0;
    for (DWORD index = static_cast<DWORD>(start);
         index < length && written + 1 < site.module.size();
         ++index) {
        const wchar_t value = path[index];
        site.module[written++] = value > 0 && value < 128 ? static_cast<char>(value) : '?';
    }
    return site;
}

[[nodiscard]] std::string_view name_stage(NameOperation operation) noexcept {
    if (operation == NameOperation::resolve) {
        return "resolve";
    }
    if (operation == NameOperation::reverse) {
        return "reverse";
    }
    return operation == NameOperation::dns ? "dns" : "control";
}

} // namespace

// match the exact native SDR module by return address; no filename or range heuristics
bool allow_native_sdr_passthrough(const void* caller) noexcept {
    HMODULE callerOwner{};
    if (caller == nullptr
        || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(caller),
                              &callerOwner)
               == FALSE) {
        return false;
    }

    const HMODULE nativeSdr = GetModuleHandleW(L"steamnetworkingsockets.dll");
    if (nativeSdr == nullptr || callerOwner != nativeSdr) {
        return false;
    }

    if (!g_nativeSdrAllowReported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client, core::log::Level::info, kNativeSdrAllow);
    }
    return true;
}

void report_native_sdr_io(NativeSdrPath path, bool connectedRoute, std::size_t bytes,
                          int apiReturn, int socketError) noexcept {
    const auto pathIndex = static_cast<std::size_t>(path);
    if (pathIndex >= kNativeSdrPathNames.size()) {
        return;
    }
    auto& counts = g_nativeSdrCounts[pathIndex * 2U + (connectedRoute ? 1U : 0U)];
    counts.bytes.fetch_add(bytes, std::memory_order_relaxed);
    std::uint32_t statusCount = 0;
    if (apiReturn != SOCKET_ERROR) {
        statusCount = counts.immediate.fetch_add(1, std::memory_order_relaxed) + 1U;
    } else if (socketError == WSA_IO_PENDING) {
        statusCount = counts.pending.fetch_add(1, std::memory_order_relaxed) + 1U;
    } else if (socketError == WSAEWOULDBLOCK) {
        statusCount = counts.wouldBlock.fetch_add(1, std::memory_order_relaxed) + 1U;
    } else {
        statusCount = counts.errors.fetch_add(1, std::memory_order_relaxed) + 1U;
    }
    const auto count = counts.calls.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (statusCount != 1U && (count & (count - 1U)) != 0U) {
        return;
    }
    std::array<char, 272> line{};
    const int written = std::snprintf(line.data(), line.size(),
                                      "ev=egress stage=native_sdr_io path=%s route=%s"
                                      " calls=%u bytes=%llu immediate=%u pending=%u"
                                      " would_block=%u errors=%u",
                                      kNativeSdrPathNames[pathIndex],
                                      connectedRoute ? "connected" : "explicit", count,
                                      static_cast<unsigned long long>(
                                          counts.bytes.load(std::memory_order_relaxed)),
                                      counts.immediate.load(std::memory_order_relaxed),
                                      counts.pending.load(std::memory_order_relaxed),
                                      counts.wouldBlock.load(std::memory_order_relaxed),
                                      counts.errors.load(std::memory_order_relaxed));
    if (written > 0) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         {line.data(), (std::min)(static_cast<std::size_t>(written),
                                                 line.size() - 1U)});
    }
}

void log_decision(SocketOperation operation, bool targetsRedirect, bool allowed) noexcept {
    if (allowed) {
        return;
    }
    const std::string_view event =
        operation == SocketOperation::connect
            ? (targetsRedirect ? kConnectDenyRedirect : kConnectDenyOther)
            : (targetsRedirect ? kSendDenyRedirect : kSendDenyOther);
    core::log::write(core::log::Channel::client, core::log::Level::warn, event);
}

void log_name_decision(NameOperation operation,
                       bool allowed,
                       std::string_view name,
                       unsigned selector,
                       unsigned status,
                       const void* caller) noexcept {
    const std::string_view stage = name_stage(operation);
    const CallerSite site = inspect_caller(caller);
    const std::size_t nameLength = name.size() < kNameLimit ? name.size() : kNameLimit;
    std::array<char, kNameEventCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=egress stage=%.*s target=redirect action=%s "
                      "selector=0x%X status=0x%X caller=%s+0x%llX name=%.*s result=ok",
                      static_cast<int>(stage.size()),
                      stage.data(),
                      allowed ? "allow" : "deny",
                      selector,
                      status,
                      site.module.data(),
                      static_cast<unsigned long long>(site.offset),
                      static_cast<int>(nameLength),
                      name.data());
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::debug, {line.data(), length});
}

void log_name_decision(NameOperation operation,
                       bool allowed,
                       std::wstring_view name,
                       unsigned selector,
                       unsigned status,
                       const void* caller) noexcept {
    std::array<char, kNameLimit + 1> narrowed{};
    const std::size_t length = name.size() < kNameLimit ? name.size() : kNameLimit;
    for (std::size_t index = 0; index < length; ++index) {
        const wchar_t value = name[index];
        narrowed[index] = value > 0 && value < 128 ? static_cast<char>(value) : '?';
    }
    log_name_decision(
        operation, allowed, std::string_view(narrowed.data(), length), selector, status, caller);
}

} // namespace dawn::client::hooks::egress::policy
