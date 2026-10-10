#include <Windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "../../core/logging/log.h"
#include "native_network_diagnostics.h"
#include "native_connection_diagnostic_handles.h"
#include "native_diagnostic_queue.h"

namespace dawn::steam::runtime::native_network_diagnostics {
namespace {
using Output = void (*)(int, const char*);
using SetOutput = void (*)(void*, int, Output);
using Accessor = void* (*)();
using RelayStatus = int (*)(void*, void*);
using QuickStatus = bool (*)(void*, std::uint32_t, void*);
std::atomic<Output> g_previous{};
std::atomic_int g_previousLevel{};
std::atomic_uint64_t g_lines{};
void* g_utils{};
SetOutput g_setOutput{};
RelayStatus g_relayStatus{};
Output* g_nativeOutput{};
std::uint64_t g_nextSample{};
int g_lastRelay = -1000;
SRWLOCK g_control{SRWLOCK_INIT};
bool g_stopped{true};
HMODULE g_callbackOwner{};
HMODULE g_nativeOwner{};
NativeConnectionDiagnosticHandles g_connections{};
SRWLOCK g_qualityControl{SRWLOCK_INIT};
std::atomic_bool g_qualityStopped{true};
void* g_qualitySockets{};
QuickStatus g_quickStatus{};
std::uint64_t g_nextQualitySample{}, g_qualityLookupMisses{}, g_lastCapacityMisses{};
NativeDiagnosticQueue<> g_debugQueue{};
std::atomic_uint64_t g_debugGeneration{};
std::uint64_t g_lastQueueRejected{};

bool readable(const void* address, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    if (address == nullptr || VirtualQuery(address, &memory, sizeof memory) != sizeof memory
        || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return begin <= end && size <= end - begin;
}

void output(int level, const char* message) noexcept {
    const auto generation = g_debugGeneration.load(std::memory_order_acquire);
    const bool accepting = !g_qualityStopped.load(std::memory_order_acquire);
    const auto previous = g_previous.load(std::memory_order_acquire);
    if (previous != nullptr && previous != &output
        && level <= g_previousLevel.load(std::memory_order_relaxed)) previous(level, message);
    if (message == nullptr) return;
    const auto sequence = g_lines.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!accepting || g_debugGeneration.load(std::memory_order_acquire) != generation) return;
    // Valve can call this while holding its transport mutex, so never format, take logger
    // locks, write a file, or wait here
    (void)g_debugQueue.try_push(level, sequence, generation, {message, strnlen_s(message, 1200)}, GetTickCount64());
}

void drain_debug_queue() noexcept {
    const auto generation = g_debugGeneration.load(std::memory_order_acquire);
    NativeDiagnosticEvent event{};
    for (std::size_t count = 0; count < 128 && g_debugQueue.try_pop(event); ++count) {
        if (event.generation != generation) continue;
        g_connections.observe({event.text.data(), event.length});
        std::array<char, 1400> line{};
        const auto now = GetTickCount64();
        const auto delay = event.observedTickMs != 0 && now >= event.observedTickMs ? now - event.observedTickMs : 0;
        const int size = std::snprintf(line.data(), line.size(),
            "ev=native_transport_diagnostic source=steam_debug severity=%d message_sequence=%llu"
            " debug_queue_delay_ms=%llu text=%s",
            event.level, static_cast<unsigned long long>(event.sequence),
            static_cast<unsigned long long>(delay), event.text.data());
        if (size > 0) core::log::write(core::log::Channel::client,
            event.level <= 2 ? core::log::Level::error : event.level == 4 ? core::log::Level::warn
                                                                       : core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(size), line.size() - 1)});
    }
    const auto rejected = g_debugQueue.rejected.load(std::memory_order_relaxed);
    if (rejected != g_lastQueueRejected) {
        g_lastQueueRejected = rejected;
        std::array<char, 192> line{};
        const int size = std::snprintf(line.data(), line.size(),
            "ev=native_transport_diagnostic source=debug_queue result=overflow capacity=128"
            " diagnostic_messages_rejected=%llu", static_cast<unsigned long long>(rejected));
        if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::warn,
            {line.data(), static_cast<std::size_t>(size)});
    }
}

bool initialize() noexcept {
    const auto module = GetModuleHandleW(L"steamnetworkingsockets.dll");
    if (module == nullptr) return false;
    auto* base = reinterpret_cast<std::byte*>(module);
    // the SDK has a setter but no getter; read the previous callback only for the exact
    // shipped setter, whose stores prove these two native data locations
    constexpr std::array<unsigned char, 39> setter{
        0x48,0x85,0xD2,0x74,0x12,0x85,0xC9,0x7E,0x0E,
        0x48,0x89,0x15,0x20,0x79,0x23,0x00,0x89,0x0D,0x62,0x77,0x23,0x00,
        0xC3,0x33,0xC0,0x48,0x89,0x05,0x10,0x79,0x23,0x00,
        0x89,0x05,0x52,0x77,0x23,0x00,0xC3};
    if (!readable(base + 0xF5C30, setter.size())
        || std::memcmp(base + 0xF5C30, setter.data(), setter.size()) != 0
        || !readable(base + 0x32D560, sizeof(Output))
        || !readable(base + 0x32D3A8, sizeof(int))) return false;
    const auto accessor = reinterpret_cast<Accessor>(GetProcAddress(module, "SteamNetworkingUtils_Lib"));
    const auto set = reinterpret_cast<SetOutput>(GetProcAddress(module,
        "SteamAPI_ISteamNetworkingUtils_SetDebugOutputFunction"));
    const auto relay = reinterpret_cast<RelayStatus>(GetProcAddress(module,
        "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus"));
    if (accessor == nullptr || set == nullptr || relay == nullptr) return false;
    void* const utils = accessor();
    if (!readable(utils, sizeof(void*))) return false;
    const auto methods = *static_cast<void***>(utils);
    if (!readable(methods, 14 * sizeof(void*))
        || methods[13] != base + 0xE1F30 || methods[1] != base + 0xB1610) return false;
    const auto previous = *reinterpret_cast<Output*>(base + 0x32D560);
    const int previousLevel = *reinterpret_cast<int*>(base + 0x32D3A8);
    if (previousLevel < 0 || previousLevel > 8 || previous == &output) return false;
    // native workers may have already copied the function pointer when shutdown detaches it,
    // so pin our callback code for the process lifetime
    if (g_callbackOwner == nullptr && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&output), &g_callbackOwner) == FALSE) return false;
    if (g_nativeOwner == nullptr && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(accessor), &g_nativeOwner) == FALSE) return false;
    g_previous.store(previous, std::memory_order_release);
    g_previousLevel.store(previousLevel, std::memory_order_release);
    g_utils = utils;
    g_setOutput = set;
    g_relayStatus = relay;
    g_nativeOutput = reinterpret_cast<Output*>(base + 0x32D560);
    set(utils, (std::max)(5, previousLevel), &output);
    return true;
}

bool initialize_quality() noexcept {
    HMODULE module{};
    // quality sampling reuses a worker; keep the API and vtable code alive through shutdown
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"steamnetworkingsockets.dll", &module)) return false;
    auto* const base = reinterpret_cast<std::byte*>(module);
    constexpr std::array<unsigned char, 8> accessorCode{0x48,0x8B,0x05,0x81,0x31,0x25,0x00,0xC3};
    constexpr std::array<unsigned char, 10> quickCode{0x48,0x8B,0x01,0x48,0xFF,0xA0,0x80,0,0,0};
    constexpr std::array<unsigned char, 8> populateCall{0x48,0x8B,0xC8,0xE8,0x7A,0x89,0,0};
    constexpr std::array<unsigned char, 39> queueFields{
        0xE8,0xB3,0xF7,0xFF,0xFF,0x89,0x47,0x20,0x8B,0x83,0xA8,0x36,0,0,0x89,0x47,0x24,
        0x8B,0x83,0xAC,0x36,0,0,0x89,0x47,0x28,0x8B,0x83,0xB0,0x36,0,0,0x89,0x47,0x2C,
        0x83,0xBB,0x98,0x37};
    const auto accessor = reinterpret_cast<Accessor>(GetProcAddress(module, "SteamNetworkingSockets_Lib"));
    const auto quick = reinterpret_cast<QuickStatus>(GetProcAddress(module,
        "SteamAPI_ISteamNetworkingSockets_GetQuickConnectionStatus"));
    if (reinterpret_cast<void*>(accessor) != base + 0xD9C80
        || reinterpret_cast<void*>(quick) != base + 0xF0500
        || !readable(base + 0xD9C80, accessorCode.size())
        || std::memcmp(base + 0xD9C80, accessorCode.data(), accessorCode.size()) != 0
        || !readable(base + 0xF0500, quickCode.size())
        || std::memcmp(base + 0xF0500, quickCode.data(), quickCode.size()) != 0
        || !readable(base + 0xE06DE, populateCall.size())
        || std::memcmp(base + 0xE06DE, populateCall.data(), populateCall.size()) != 0
        || !readable(base + 0xFAC88, queueFields.size())
        || std::memcmp(base + 0xFAC88, queueFields.data(), queueFields.size()) != 0) return false;
    void* const sockets = accessor();
    if (!readable(sockets, sizeof(void*))) return false;
    auto** const methods = *static_cast<void***>(sockets);
    if (reinterpret_cast<void*>(methods) != base + 0x24BC40
        || !readable(methods, 18 * sizeof(void*)) || methods[16] != base + 0xE0690) return false;
    g_qualitySockets = sockets;
    g_quickStatus = quick;
    return true;
}

template<class T> T quick_field(const std::array<std::byte, 512>& bytes, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}
}

void start() noexcept {
    AcquireSRWLockExclusive(&g_control);
    g_stopped = false;
    g_nextSample = 0;
    g_connections.clear();
    g_debugGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_qualityStopped.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_control);
}

void service(std::uint64_t now) noexcept {
    AcquireSRWLockExclusive(&g_control);
    if (g_stopped || now < g_nextSample) {
        ReleaseSRWLockExclusive(&g_control);
        return;
    }
    g_nextSample = now + 5000;
    if (g_utils == nullptr && !initialize()) {
        ReleaseSRWLockExclusive(&g_control);
        return;
    }
    const auto relayStatus = g_relayStatus;
    void* const utils = g_utils;
    Output* const nativeOutput = g_nativeOutput;
    ReleaseSRWLockExclusive(&g_control);
    // never hold our lifecycle lock while entering the native relay implementation
    const int relay = relayStatus(utils, nullptr);
    AcquireSRWLockExclusive(&g_control);
    if (g_stopped) {
        ReleaseSRWLockExclusive(&g_control);
        return;
    }
    const bool changed = relay != g_lastRelay;
    g_lastRelay = relay;
    ReleaseSRWLockExclusive(&g_control);
    std::array<char, 240> line{};
    const int size = std::snprintf(line.data(), line.size(),
        "ev=native_transport_diagnostic source=relay_status availability=%d changed=%u"
        " debug_callback_owned=%u native_debug_messages=%llu",
        relay, changed ? 1U : 0U, *nativeOutput == &output ? 1U : 0U,
        static_cast<unsigned long long>(g_lines.load(std::memory_order_relaxed)));
    if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::info,
        {line.data(), static_cast<std::size_t>(size)});
}

void service_connection_quality(std::uint64_t now) noexcept {
    if (g_qualityStopped.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_qualityControl);
    if (g_qualityStopped.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_qualityControl);
        return;
    }
    drain_debug_queue();
    if (now < g_nextQualitySample) {
        ReleaseSRWLockExclusive(&g_qualityControl);
        return;
    }
    g_nextQualitySample = now + 5000;
    const auto handles = g_connections.snapshot();
    const bool any = std::any_of(handles.begin(), handles.end(), [](auto id) { return id != 0; });
    if (any && g_quickStatus == nullptr && !initialize_quality()) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=native_transport_diagnostic source=connection_quality result=unavailable reason=unsupported_native_api");
        ReleaseSRWLockExclusive(&g_qualityControl);
        return;
    }
    for (const auto handle : handles) {
        if (!handle || g_qualityStopped.load(std::memory_order_acquire)) continue;
        // the shipped implementation writes offsets 0..37; oversized zeroed storage keeps the
        // reserved tail without borrowing a newer SDK struct
        alignas(8) std::array<std::byte, 512> status{};
        if (!g_quickStatus(g_qualitySockets, handle, status.data())) {
            g_connections.erase(handle);
            ++g_qualityLookupMisses;
            std::array<char, 192> line{};
            const int size = std::snprintf(line.data(), line.size(),
                "ev=native_transport_diagnostic source=connection_quality result=lookup_unavailable"
                " connection=%u handle_lookup_misses=%llu", handle,
                static_cast<unsigned long long>(g_qualityLookupMisses));
            if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::info,
                {line.data(), static_cast<std::size_t>(size)});
            continue;
        }
        const auto local = quick_field<float>(status, 8), remote = quick_field<float>(status, 12);
        const auto txPackets = quick_field<float>(status, 16), txBytes = quick_field<float>(status, 20);
        const auto rxPackets = quick_field<float>(status, 24), rxBytes = quick_field<float>(status, 28);
        if (!std::isfinite(local) || !std::isfinite(remote) || !std::isfinite(txPackets)
            || !std::isfinite(txBytes) || !std::isfinite(rxPackets) || !std::isfinite(rxBytes)) continue;
        std::array<char, 640> line{};
        const int size = std::snprintf(line.data(), line.size(),
            "ev=native_transport_diagnostic source=connection_quality result=sample connection=%u"
            " state=%d ping_ms=%d quality_local=%.5f quality_remote=%.5f"
            " tx_packets_sec=%.2f tx_bytes_sec=%.2f rx_packets_sec=%.2f rx_bytes_sec=%.2f"
            " send_rate_bytes_sec=%d pending_unreliable_bytes=%d pending_reliable_bytes=%d"
            " sent_unacked_reliable_bytes=%d queue_time_us=%lld handle_lookup_misses=%llu",
            handle, quick_field<int>(status, 0), quick_field<int>(status, 4),
            static_cast<double>(local), static_cast<double>(remote), static_cast<double>(txPackets),
            static_cast<double>(txBytes), static_cast<double>(rxPackets), static_cast<double>(rxBytes),
            quick_field<int>(status, 32), quick_field<int>(status, 36), quick_field<int>(status, 40),
            quick_field<int>(status, 44), static_cast<long long>(quick_field<std::int64_t>(status, 48)),
            static_cast<unsigned long long>(g_qualityLookupMisses));
        if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(size), line.size() - 1)});
    }
    const auto misses = g_connections.capacityMisses.load(std::memory_order_relaxed);
    if (misses != g_lastCapacityMisses) {
        g_lastCapacityMisses = misses;
        std::array<char, 160> line{};
        const int size = std::snprintf(line.data(), line.size(),
            "ev=native_transport_diagnostic source=connection_quality result=tracker_capacity capacity=16"
            " debug_handle_admission_misses=%llu", static_cast<unsigned long long>(misses));
        if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::warn,
            {line.data(), static_cast<std::size_t>(size)});
    }
    ReleaseSRWLockExclusive(&g_qualityControl);
}

void shutdown() noexcept {
    g_qualityStopped.store(true, std::memory_order_release);
    g_debugGeneration.fetch_add(1, std::memory_order_acq_rel);
    AcquireSRWLockExclusive(&g_qualityControl);
    g_qualitySockets = nullptr;
    g_quickStatus = nullptr;
    g_nextQualitySample = g_qualityLookupMisses = g_lastCapacityMisses = 0;
    g_connections.clear();
    ReleaseSRWLockExclusive(&g_qualityControl);
    AcquireSRWLockExclusive(&g_control);
    g_stopped = true;
    // the validated setter is two stores plus return; it takes no native lock and invokes no
    // callbacks, so it cannot reenter this lock
    if (g_setOutput != nullptr && readable(g_nativeOutput, sizeof(Output))
        && *g_nativeOutput == &output)
        g_setOutput(g_utils, g_previousLevel.load(std::memory_order_relaxed),
                    g_previous.load(std::memory_order_acquire));
    g_utils = nullptr;
    g_setOutput = nullptr;
    g_relayStatus = nullptr;
    g_nativeOutput = nullptr;
    g_nextSample = 0;
    g_lastRelay = -1000;
    ReleaseSRWLockExclusive(&g_control);
}
} // namespace dawn::steam::runtime::native_network_diagnostics
