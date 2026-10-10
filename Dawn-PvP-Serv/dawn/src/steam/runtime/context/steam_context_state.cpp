#include "steam_context_state.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "../../interfaces/steam_interface_factory.h"
#include "../internal.h"
#include "../runtime.h"

namespace dawn::steam {
namespace {

constexpr UserHandle kUserHandle = 1;
constexpr UserHandle kGlobalUserHandle = 0;
constexpr PipeHandle kPipeHandle = 1;
constexpr ApiCall kFirstApiCall = 1;
constexpr std::uintptr_t kFirstContextGeneration = 1;
constexpr char kNetworkingClientVersion[] = "SteamClient018";

enum class ContextField : std::size_t {
    initializer = 0,
    generation = 1,
    interface = 2,
};

SRWLOCK g_contextLock{SRWLOCK_INIT};
std::atomic_uintptr_t g_contextGeneration{kFirstContextGeneration};
std::atomic<DWORD> g_appId{};
std::atomic<ApiCall> g_nextApiCall{kFirstApiCall};

} // namespace

void set_app_id(DWORD appId) noexcept {
    g_appId.store(appId, std::memory_order_release);
}

DWORD app_id() noexcept {
    return g_appId.load(std::memory_order_acquire);
}

ApiCall next_api_call() noexcept {
    ApiCall call = g_nextApiCall.fetch_add(1, std::memory_order_relaxed);
    if (call != 0) {
        return call;
    }
    // the counter can wrap to Steam's reserved zero id; skip it once
    call = g_nextApiCall.fetch_add(1, std::memory_order_relaxed);
    return call == 0 ? kFirstApiCall : call;
}

UserHandle user_handle() noexcept {
    return kUserHandle;
}

PipeHandle pipe_handle() noexcept {
    return kPipeHandle;
}

void* context_init(void* data) noexcept {
    if (data == nullptr) {
        return nullptr;
    }
    auto** fields = static_cast<void**>(data);
    const auto initializerIndex = static_cast<std::size_t>(ContextField::initializer);
    const auto generationIndex = static_cast<std::size_t>(ContextField::generation);
    const auto interfaceIndex = static_cast<std::size_t>(ContextField::interface);
    const auto generation = g_contextGeneration.load(std::memory_order_acquire);
    std::uintptr_t storedGeneration{};
    std::memcpy(&storedGeneration, &fields[generationIndex], sizeof(storedGeneration));

    AcquireSRWLockExclusive(&g_contextLock);
    if (storedGeneration != generation) {
        fields[interfaceIndex] = nullptr;
        const auto initializer = reinterpret_cast<void (*)(void*)>(fields[initializerIndex]);
        if (initializer != nullptr) {
            initializer(&fields[interfaceIndex]);
        }
        // write the generation after setup so no reader accepts stale storage
        std::memcpy(&fields[generationIndex], &generation, sizeof(generation));
    }
    void* result = &fields[interfaceIndex];
    ReleaseSRWLockExclusive(&g_contextLock);
    return result;
}

void* create_interface(const char* version, const void* callerAddress) noexcept {
    void* const interface = interfaces::create(version);
    if (interface != nullptr && std::strcmp(version, kNetworkingClientVersion) == 0) {
        runtime::activate_platform_once(callerAddress);
    }
    return interface;
}

void* find_or_create_user_interface(UserHandle user, const char* version) noexcept {
    if (user == kGlobalUserHandle) {
        return interfaces::find_global(version);
    }
    return user == kUserHandle ? interfaces::find_user(version) : nullptr;
}

} // namespace dawn::steam

namespace dawn::steam::context {

void advance_generation() noexcept {
    g_contextGeneration.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace dawn::steam::context
