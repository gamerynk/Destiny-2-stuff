#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

#include "../runtime.h"

namespace dawn::steam::runtime::callbacks {

// in this build the call-result overload comes first, then the regular overload and the
// size query
enum class CallbackMethod : std::size_t {
    callResult = 0,
    regular = 1,
    payloadSize = 2,
};

void clear() noexcept;

inline constexpr std::size_t kCallbackCapacity = 256;
inline constexpr std::size_t kCallResultCapacity = 256;
inline constexpr std::size_t kEventCapacity = 64;
// also covers the archived transport's 792-byte certificate result
inline constexpr std::size_t kEventPayloadCapacity = 16 + 4096;

struct CallbackEntry {
    void* callback{};
    int callbackId{};
};

struct CallResultEntry {
    void* callback{};
    ApiCall call{};
};

// Steam routes a registered async result only by its API-call handle; the callback id
// selects the payload type and is not a second key
[[nodiscard]] constexpr bool matches_call_result(const CallResultEntry& entry,
                                                 ApiCall call) noexcept {
    return entry.callback != nullptr && call != 0 && entry.call == call;
}

[[nodiscard]] bool has_call_result(ApiCall call) noexcept;

struct CallbackEvent {
    int callbackId{};
    ApiCall call{};
    std::size_t payloadSize{};
    std::array<std::byte, kEventPayloadCapacity> payload{};
};

extern SRWLOCK g_lock;
extern std::array<CallbackEntry, kCallbackCapacity> g_callbacks;
extern std::array<CallResultEntry, kCallResultCapacity> g_callResults;
extern std::array<CallbackEvent, kEventCapacity> g_events;
extern std::size_t g_eventHead;
extern std::size_t g_eventCount;

void stage_lifecycle_events() noexcept;

[[nodiscard]] bool record_lifecycle_delivery(int callbackId) noexcept;

[[nodiscard]] bool push_event_locked(int callbackId,
                                     ApiCall call,
                                     const void* payload,
                                     std::size_t payloadSize) noexcept;

void set_registration(void* callback, bool registered) noexcept;

[[nodiscard]] int callback_id(void* callback) noexcept;
} // namespace dawn::steam::runtime::callbacks
