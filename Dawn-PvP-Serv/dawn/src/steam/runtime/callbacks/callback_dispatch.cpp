#include "../../../client/hooks/graphics/graphics_hook_lifecycle.h"
#include "../../../client/hooks/polled_input/runtime.h"
#include "../../../client/hooks/cursor/runtime.h"
#include <Windows.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "../../../client/diagnostics/module_range.h"
#include "../../../client/hooks/network/runtime.h"
#include "../../../client/hooks/bootflow/native_fireteam_callback.h"
#include "../../../client/content/investment/worker.h"
#include "../../../core/logging/log.h"
#include "../../../core/runtime/runtime_policy.h"
#include "../../../server/runtime/server_runtime.h"
#include "../internal.h"
#include "callback_registry.h"
#include "../social/social_cache.h"
#include "../social/realtime_client.h"
#include "../transport/steam_transport_bridge.h"
#include "../native_network_diagnostics.h"

namespace dawn::steam::runtime::callbacks {
namespace {



constexpr int kNativeRendezvousEvent = -0x210;
// build 9704 offset of the RecvP2PRendezvous callback subobject
constexpr std::uintptr_t kNativeRendezvousCallbackOffset = 0x7B78U;
// build 9704 callback vtable and method RVAs in the shipped networking image
constexpr std::uintptr_t kNativeRendezvousVtableRva = 0x24BE70U;
constexpr std::uintptr_t kNativeRendezvousRegularRva = 0xD89E0U;
constexpr std::uintptr_t kNativeRendezvousPayloadSizeRva = 0xD7820U;

[[nodiscard]] int callback_payload_size(void* callback) noexcept;
void invoke_callback(void* callback, void* payload) noexcept;

[[nodiscard]] bool readable_range(const void* address, std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof memory) != sizeof memory
        || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto regionBegin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto regionEnd = regionBegin + memory.RegionSize;
    return begin >= regionBegin && begin <= regionEnd && size <= regionEnd - begin;
}

// only resolves when every build 9704 invariant matches; the public callback registry
// never owns this object
[[nodiscard]] void* native_rendezvous_callback(const char*& failure) noexcept {
    failure = "module_missing";
    HMODULE module = GetModuleHandleW(L"steamnetworkingsockets.dll");
    client::diagnostics::ModuleRange range{};
    if (module == nullptr || !client::diagnostics::module_range(module, range)) {
        return nullptr;
    }

    failure = "export_missing";
    FARPROC exportAddress = GetProcAddress(module, "SteamNetworkingSockets_Lib");
    if (exportAddress == nullptr
        || !client::diagnostics::contains(
            range, reinterpret_cast<std::uintptr_t>(exportAddress))) {
        return nullptr;
    }

    using Accessor = void* (*)();
    void* instance = reinterpret_cast<Accessor>(exportAddress)();
    failure = "instance_missing";
    if (instance == nullptr) {
        return nullptr;
    }

    auto* callback = static_cast<std::byte*>(instance) + kNativeRendezvousCallbackOffset;
    failure = "callback_unreadable";
    if (!readable_range(callback, sizeof(void*))) {
        return nullptr;
    }
    auto** methods = *reinterpret_cast<void***>(callback);
    failure = "vtable_unreadable";
    if (!readable_range(methods, 3U * sizeof(void*))) {
        return nullptr;
    }

    const auto vtable = reinterpret_cast<std::uintptr_t>(methods);
    const auto regular = reinterpret_cast<std::uintptr_t>(
        methods[static_cast<std::size_t>(CallbackMethod::regular)]);
    const auto payloadSize = reinterpret_cast<std::uintptr_t>(
        methods[static_cast<std::size_t>(CallbackMethod::payloadSize)]);
    failure = "build_mismatch";
    if (vtable != range.base + kNativeRendezvousVtableRva
        || regular != range.base + kNativeRendezvousRegularRva
        || payloadSize != range.base + kNativeRendezvousPayloadSizeRva) {
        return nullptr;
    }
    failure = "payload_size_mismatch";
    if (callback_payload_size(callback)
        != static_cast<int>(kSerializedRendezvousCallbackSize)) {
        return nullptr;
    }
    failure = "ok";
    return callback;
}

[[nodiscard]] bool dispatch_native_rendezvous(const CallbackEvent& event) noexcept {
    const char* failure = nullptr;
    void* callback = native_rendezvous_callback(failure);
    const bool validEvent = event.payloadSize == kSerializedRendezvousCallbackSize;
    if (callback != nullptr && validEvent) {
        invoke_callback(callback, const_cast<std::byte*>(event.payload.data()));
    }

    std::uint64_t remotePlatformId{};
    std::uint32_t sourceConnectionId{};
    std::uint32_t packetSize{};
    if (event.payloadSize >= 16U) {
        std::memcpy(&remotePlatformId, event.payload.data(), sizeof(remotePlatformId));
        std::memcpy(&sourceConnectionId, event.payload.data() + 8, sizeof(sourceConnectionId));
        std::memcpy(&packetSize, event.payload.data() + 12, sizeof(packetSize));
    }
    std::array<char, 384> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=rendezvous_callback result=%s route=native_instance failure=%s"
        " remote=0x%016llX source_connection=%u payload_bytes=%u advertised_bytes=%zu",
        callback != nullptr && validEvent ? "delivered" : "rejected",
        callback == nullptr ? failure : (validEvent ? "none" : "event_size"),
        static_cast<unsigned long long>(remotePlatformId),
        sourceConnectionId,
        packetSize,
        event.payloadSize);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         callback != nullptr && validEvent ? core::log::Level::info
                                                           : core::log::Level::error,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    return callback != nullptr && validEvent;
}

[[nodiscard]] int callback_payload_size(void* callback) noexcept {
    auto** methods = *static_cast<void***>(callback);
    const auto slot = static_cast<std::size_t>(CallbackMethod::payloadSize);
    if (methods == nullptr || methods[slot] == nullptr) {
        return 0;
    }
    const auto size = reinterpret_cast<int (*)(void*)>(methods[slot]);
    return size(callback);
}

void invoke_callback(void* callback, void* payload) noexcept {
    auto** methods = *static_cast<void***>(callback);
    const auto slot = static_cast<std::size_t>(CallbackMethod::regular);
    if (methods == nullptr || methods[slot] == nullptr) {
        return;
    }
    const auto run = reinterpret_cast<void (*)(void*, void*)>(methods[slot]);
    run(callback, payload);
}

void invoke_call_result(void* callback, void* payload, ApiCall call) noexcept {
    auto** methods = *static_cast<void***>(callback);
    const auto slot = static_cast<std::size_t>(CallbackMethod::callResult);
    if (methods == nullptr || methods[slot] == nullptr) {
        return;
    }
    const auto run = reinterpret_cast<void (*)(void*, void*, bool, ApiCall)>(methods[slot]);
    run(callback, payload, false, call);
}

[[nodiscard]] bool pop_event(CallbackEvent& event) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_eventCount == 0) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    event = g_events[g_eventHead];
    g_events[g_eventHead] = {};
    g_eventHead = (g_eventHead + 1) % kEventCapacity;
    --g_eventCount;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void dispatch_event(CallbackEvent& event) noexcept {
    constexpr int kGameRichPresenceJoinRequestedCallback = 337;
    if (event.callbackId == kNativeRendezvousEvent) {
        (void)dispatch_native_rendezvous(event);
        return;
    }
    std::array<void*, kCallbackCapacity> callbacks{};
    std::array<void*, kCallResultCapacity> callResults{};
    std::size_t callbackCount{};
    std::size_t callResultCount{};
    AcquireSRWLockExclusive(&g_lock);
    for (const auto& entry : g_callbacks) {
        if (entry.callback != nullptr && entry.callbackId == event.callbackId) {
            callbacks[callbackCount++] = entry.callback;
        }
    }
    if (event.call != 0) {
        for (auto& entry : g_callResults) {
            // the callback id inside CCallbackBase is metadata, not a routing key. Some private
            // SteamNetworkingSockets call-results leave it out of sync; filtering dropped a valid cert
            if (matches_call_result(entry, event.call)) {
                callResults[callResultCount++] = entry.callback;
                entry = {};
            }
        }
    }
    // callback code can register again from inside the call, so call after the unlock
    ReleaseSRWLockExclusive(&g_lock);
    if (event.callbackId == kGameRichPresenceJoinRequestedCallback) {
        social::realtime_client::report_owner_gameplay_admission_dispatch(
            event.payload.data(), event.payloadSize, callbackCount);
    }
    for (std::size_t index = 0; index < callbackCount; ++index) {
        invoke_callback(callbacks[index], event.payload.data());
    }
    if (event.callbackId == kGameRichPresenceJoinRequestedCallback) {
        social::realtime_client::report_owner_gameplay_admission_dispatch_return(
            event.payload.data(), event.payloadSize, callbackCount);
    }
    if (event.callbackId == 1296) {
        std::array<char, 256> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=steam_transport stage=certificate_dispatch result=%s"
            " regular=%zu call_results=%zu bytes=%zu",
            callResultCount != 0 ? "delivering" : "unregistered",
            callbackCount,
            callResultCount,
            event.payloadSize);
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                callResultCount != 0 ? core::log::Level::info : core::log::Level::error,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
    }
    for (std::size_t index = 0; index < callResultCount; ++index) {
        invoke_call_result(callResults[index], event.payload.data(), event.call);
    }
}

} // namespace
} // namespace dawn::steam::runtime::callbacks

namespace dawn::steam {

void run_callbacks() noexcept {
    client::hooks::bootflow::NativeFireteamCallbackScope nativeFireteamScope{};

    constexpr int kSteamServersConnectedCallback = 101;
    // install presentation before activating the main game hooks
    runtime::activate_graphics_once();
    if (client::hooks::bootflow::native_fireteam_callback_is_outermost()) {
        client::hooks::graphics::service_native_window();
        client::hooks::polled_input::service_native_key_recovery();
        client::hooks::cursor::service_native_cursor_tail();

    }
    // the network group must own SignOn before callback work can send it
    (void)runtime::activate_main_once();
    client::hooks::bootflow::service_native_fireteam_callback();
    if (!client::hooks::network::service_platform_policy()) return;
    runtime::transport::service();
    if (!client::hooks::network::service_platform_policy()) return;
    // the archived client registers three static connection consumers, then the networking
    // module adds a fourth. admit sticky lifecycle events only after that full census exists
    runtime::callbacks::stage_lifecycle_events();
    // async operations started by a callback must not complete in the same batch; snapshot
    // the queue so events queued while dispatching wait for the next SteamAPI_RunCallbacks
    AcquireSRWLockShared(&runtime::callbacks::g_lock);
    const std::size_t batchCount = runtime::callbacks::g_eventCount;
    ReleaseSRWLockShared(&runtime::callbacks::g_lock);
    runtime::callbacks::CallbackEvent event;
    for (std::size_t count = 0;
         count < batchCount && runtime::callbacks::pop_event(event);
         ++count) {
        runtime::callbacks::dispatch_event(event);
        const bool lifecycleComplete =
            runtime::callbacks::record_lifecycle_delivery(event.callbackId);
        if (event.callbackId == kSteamServersConnectedCallback) {
            // callback 304 is staged on the next pump from the stable local identity; this refresh
            // only starts the server-owned friend projection and must never gate native sign-in
            const bool refreshed = runtime::social::refresh();
            runtime::social::activate();
            runtime::social::realtime_client::activate();
            core::log::write(core::log::Channel::client,
                             refreshed ? core::log::Level::info : core::log::Level::warn,
                             refreshed
                                 ? "ev=steam_lifecycle stage=identity_refresh result=ok"
                                 : "ev=steam_lifecycle stage=identity_refresh result=deferred");
        }
        if (lifecycleComplete) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=steam_lifecycle stage=identity_commit result=ok");
        }
    }
    (void)client::hooks::network::service_platform_policy();
    const auto now = GetTickCount64();
    runtime::native_network_diagnostics::service(now);
    if constexpr (!core::runtime::policy::kDedicatedBackendRequired) {
        server::service(now);
    }
    client::content::investment::worker::service(now);
    runtime::social::service(now);
    runtime::social::realtime_client::service(now);
}

bool queue_callback(int callbackId,
                    ApiCall call,
                    const void* payload,
                    std::size_t payloadSize) noexcept {
    using namespace runtime::callbacks;
    if (callbackId <= 0 || payloadSize > kEventPayloadCapacity
        || (payload == nullptr && payloadSize != 0)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=callback_queue result=invalid");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!push_event_locked(callbackId, call, payload, payloadSize)) {
        ReleaseSRWLockExclusive(&g_lock);
        core::log::write(
            core::log::Channel::client, core::log::Level::error, "ev=callback_queue result=full");
        return false;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

std::size_t registered_callback_count(int callbackId, std::size_t payloadSize) noexcept {
    using namespace runtime::callbacks;
    if (callbackId <= 0 || payloadSize == 0) {
        return 0;
    }
    std::array<void*, kCallbackCapacity> callbacks{};
    std::size_t count = 0;
    AcquireSRWLockShared(&g_lock);
    for (const CallbackEntry& entry : g_callbacks) {
        if (entry.callback != nullptr && entry.callbackId == callbackId
            && count < callbacks.size()) {
            callbacks[count++] = entry.callback;
        }
    }
    ReleaseSRWLockShared(&g_lock);

    std::size_t matching = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (callback_payload_size(callbacks[index]) == static_cast<int>(payloadSize)) {
            ++matching;
        }
    }
    return matching;
}

bool queue_serialized_rendezvous(std::uint64_t remotePlatformId,
                                 std::uint32_t sourceConnectionId,
                                 const void* payload,
                                 std::size_t payloadSize) noexcept {
    using namespace runtime::callbacks;
    if (remotePlatformId == 0 || payloadSize > kSerializedRendezvousPacketCapacity
        || (payload == nullptr && payloadSize != 0)) {
        return false;
    }

    // the private callback's 0x210-byte ABI is instance-owned. Keep the payload in the
    // ordinary ring so its handler runs on the SteamAPI_RunCallbacks thread, not the poller
    std::array<std::byte, kSerializedRendezvousCallbackSize> callback{};
    std::memcpy(callback.data(), &remotePlatformId, sizeof(remotePlatformId));
    std::memcpy(callback.data() + 8, &sourceConnectionId, sizeof(sourceConnectionId));
    const auto packetSize = static_cast<std::uint32_t>(payloadSize);
    std::memcpy(callback.data() + 12, &packetSize, sizeof(packetSize));
    if (payloadSize != 0) {
        std::memcpy(callback.data() + 16, payload, payloadSize);
    }
    AcquireSRWLockExclusive(&g_lock);
    const bool queued = push_event_locked(
        kNativeRendezvousEvent, 0, callback.data(), callback.size());
    ReleaseSRWLockExclusive(&g_lock);
    std::array<char, 320> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=rendezvous_callback result=%s route=native_instance remote=0x%016llX"
        " source_connection=%u payload_bytes=%zu advertised_bytes=%zu",
        queued ? "queued" : "queue_failed",
        static_cast<unsigned long long>(remotePlatformId),
        sourceConnectionId,
        payloadSize,
        callback.size());
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         queued ? core::log::Level::info : core::log::Level::error,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    return queued;
}

} // namespace dawn::steam
