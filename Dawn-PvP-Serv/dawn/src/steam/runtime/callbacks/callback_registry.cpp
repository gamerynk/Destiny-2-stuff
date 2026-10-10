#include "callback_registry.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "../../../core/logging/log.h"
namespace dawn::steam::runtime::callbacks {
namespace {

constexpr std::size_t kCallbackFlagsOffset = sizeof(void*);
constexpr std::size_t kCallbackIdOffset = sizeof(void*) + sizeof(std::uint32_t);
constexpr std::uint8_t kRegisteredFlag = 0x01;
constexpr int kSteamServersConnectedCallback = 101;
constexpr int kPersonaStateChangeCallback = 304;
// every callback-101 object must see the event: 3 main-image consumers register first,
// then steamnetworkingsockets.dll adds a 4th. It is sticky, so admitting after 3 starves it
constexpr std::size_t kSteamServersConnectedRegistrationCount = 4;
constexpr std::int32_t kPersonaChangeName = 1;

struct PersonaStateChange {
    std::uint64_t steamId{};
    std::int32_t changeFlags{};
    std::int32_t padding{};
};
static_assert(sizeof(PersonaStateChange) == 16);

void set_callback_id(void* callback, int callbackId) noexcept {
    auto* bytes = static_cast<std::byte*>(callback);
    std::memcpy(bytes + kCallbackIdOffset, &callbackId, sizeof(callbackId));
}

} // namespace

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<CallbackEntry, kCallbackCapacity> g_callbacks{};
std::array<CallResultEntry, kCallResultCapacity> g_callResults{};
std::array<CallbackEvent, kEventCapacity> g_events{};
std::size_t g_eventHead{};
std::size_t g_eventCount{};
bool g_serversConnectedQueued{};
bool g_personaStateChangeQueued{};
bool g_serversConnectedDelivered{};
bool g_personaStateChangeDelivered{};
bool g_lifecycleCompletionReported{};

bool push_event_locked(int callbackId,
                       ApiCall call,
                       const void* payload,
                       std::size_t payloadSize) noexcept {
    if (g_eventCount == kEventCapacity) {
        return false;
    }
    const std::size_t tail = (g_eventHead + g_eventCount) % kEventCapacity;
    auto& event = g_events[tail];
    event = {};
    event.callbackId = callbackId;
    event.call = call;
    event.payloadSize = payloadSize;
    if (payloadSize != 0) {
        std::memcpy(event.payload.data(), payload, payloadSize);
    }
    ++g_eventCount;
    return true;
}

void stage_lifecycle_events() noexcept {
    const std::uint64_t localSteamId = local_steam_id();
    const bool localIdentityReady = localSteamId != 0;
    std::size_t serversConnectedRegistrations{};
    std::size_t personaStateChangeRegistrations{};
    bool queuedServersConnected{};
    bool queuedPersonaStateChange{};
    AcquireSRWLockExclusive(&g_lock);
    for (const auto& entry : g_callbacks) {
        if (entry.callbackId == kSteamServersConnectedCallback && entry.callback != nullptr) {
            ++serversConnectedRegistrations;
        } else if (entry.callbackId == kPersonaStateChangeCallback && entry.callback != nullptr) {
            ++personaStateChangeRegistrations;
        }
    }

    // PersonaStateChange must not wait for the social HTTP snapshot; finishing after sign-in
    // left an empty local platform record and "unknown platform account id" on later members
    if (!g_serversConnectedQueued
        && serversConnectedRegistrations >= kSteamServersConnectedRegistrationCount) {
        const std::byte payload{};
        queuedServersConnected = push_event_locked(
            kSteamServersConnectedCallback, 0, &payload, sizeof(payload));
        g_serversConnectedQueued = queuedServersConnected;
    }
    if (!g_personaStateChangeQueued && g_serversConnectedDelivered && localIdentityReady
        && personaStateChangeRegistrations != 0) {
        const PersonaStateChange payload{localSteamId, kPersonaChangeName, 0};
        queuedPersonaStateChange =
            push_event_locked(kPersonaStateChangeCallback, 0, &payload, sizeof(payload));
        g_personaStateChangeQueued = queuedPersonaStateChange;
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (queuedServersConnected) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=steam_lifecycle callback=101 result=queued");
    }
    if (queuedPersonaStateChange) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=steam_lifecycle callback=304 result=queued");
    }
}

bool record_lifecycle_delivery(int callbackId) noexcept {
    bool completed{};
    AcquireSRWLockExclusive(&g_lock);
    if (callbackId == kSteamServersConnectedCallback) {
        g_serversConnectedDelivered = true;
    } else if (callbackId == kPersonaStateChangeCallback) {
        g_personaStateChangeDelivered = true;
    }
    if (!g_lifecycleCompletionReported && g_serversConnectedDelivered
        && g_personaStateChangeDelivered) {
        g_lifecycleCompletionReported = true;
        completed = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return completed;
}

bool has_call_result(ApiCall call) noexcept {
    if (call == 0) {
        return false;
    }
    bool found{};
    AcquireSRWLockShared(&g_lock);
    for (const auto& entry : g_callResults) {
        if (matches_call_result(entry, call)) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

void set_registration(void* callback, bool registered) noexcept {
    if (callback == nullptr) {
        return;
    }
    auto* bytes = static_cast<std::byte*>(callback);
    std::uint8_t flags{};
    std::memcpy(&flags, bytes + kCallbackFlagsOffset, sizeof(flags));
    flags = registered ? static_cast<std::uint8_t>(flags | kRegisteredFlag)
                       : static_cast<std::uint8_t>(flags & ~kRegisteredFlag);
    std::memcpy(bytes + kCallbackFlagsOffset, &flags, sizeof(flags));
}

int callback_id(void* callback) noexcept {
    int callbackId{};
    const auto* bytes = static_cast<const std::byte*>(callback);
    std::memcpy(&callbackId, bytes + kCallbackIdOffset, sizeof(callbackId));
    return callbackId;
}

} // namespace dawn::steam::runtime::callbacks

namespace dawn::steam {

void register_callback(void* callback, int callbackId) noexcept {
    using namespace runtime::callbacks;
    if (callback == nullptr) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    CallbackEntry* freeEntry = nullptr;
    for (auto& entry : g_callbacks) {
        if (entry.callback == callback) {
            entry.callbackId = callbackId;
            set_callback_id(callback, callbackId);
            set_registration(callback, true);
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        if (freeEntry == nullptr && entry.callback == nullptr) {
            freeEntry = &entry;
        }
    }
    if (freeEntry != nullptr) {
        *freeEntry = CallbackEntry{callback, callbackId};
        set_callback_id(callback, callbackId);
        set_registration(callback, true);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (freeEntry == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=callback_register result=full");
    }
}

void unregister_callback(void* callback) noexcept {
    using namespace runtime::callbacks;
    if (callback == nullptr) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (auto& entry : g_callbacks) {
        if (entry.callback == callback) {
            entry = {};
        }
    }
    set_registration(callback, false);
    ReleaseSRWLockExclusive(&g_lock);
}

void register_call_result(void* callback, ApiCall call) noexcept {
    using namespace runtime::callbacks;
    if (callback == nullptr || call == 0) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    CallResultEntry* freeEntry = nullptr;
    for (auto& entry : g_callResults) {
        if (entry.callback == callback && entry.call == call) {
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        if (freeEntry == nullptr && entry.callback == nullptr) {
            freeEntry = &entry;
        }
    }
    if (freeEntry != nullptr) {
        *freeEntry = CallResultEntry{callback, call};
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (freeEntry == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=call_result_register result=full");
    } else {
        std::array<char, 192> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=call_result_register result=ok callback=%d call=%llu",
            callback_id(callback),
            static_cast<unsigned long long>(call));
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                core::log::Level::info,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
    }
}

void unregister_call_result(void* callback, ApiCall call) noexcept {
    using namespace runtime::callbacks;
    if (callback == nullptr) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (auto& entry : g_callResults) {
        if (entry.callback == callback && (call == 0 || entry.call == call)) {
            entry = {};
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

namespace runtime::callbacks {

void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (auto& entry : g_callbacks) {
        if (entry.callback != nullptr) {
            set_registration(entry.callback, false);
        }
        entry = {};
    }
    g_callResults.fill({});
    g_events.fill({});
    g_eventHead = 0;
    g_eventCount = 0;
    g_serversConnectedQueued = false;
    g_personaStateChangeQueued = false;
    g_serversConnectedDelivered = false;
    g_personaStateChangeDelivered = false;
    g_lifecycleCompletionReported = false;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace runtime::callbacks
} // namespace dawn::steam
