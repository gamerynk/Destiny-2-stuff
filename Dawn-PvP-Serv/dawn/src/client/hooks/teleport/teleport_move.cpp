#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "internal.h"
#include "runtime.h"
namespace dawn::client::hooks::teleport {
namespace {
std::atomic<std::byte*> g_playerComponent{};
std::atomic_uint32_t g_playerHandle{kInvalidHandle};
ControlledHandle g_controlledHandle{};
NativePlayerBodyLookup g_nativePlayerLookup{};
// callbacks can observe from a native network worker while lifecycle releases the targets
SRWLOCK g_playerTargetsLock = SRWLOCK_INIT;
template <typename T> [[nodiscard]] bool read_at(const std::byte* address, T& value) noexcept {
    if (address == nullptr) {
        return false;
    }
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof value, &read) != FALSE
           && read == sizeof value;
}

[[nodiscard]] std::byte* body_of(std::byte* component) noexcept {
    if (component == nullptr) return nullptr;
    std::byte* array = nullptr;
    std::int32_t index = 0;
    if (!read_at(component + kPhysicsComponentBodyArray, array)
        || !read_at(component + kPhysicsComponentBodyIndex, index) || array == nullptr
        || index < 0 || index >= 65536) {
        return nullptr;
    }
    std::byte* body = nullptr;
    const std::size_t offset = kBodyEntryStride * static_cast<std::size_t>(index) + kBodyPointer;
    return read_at(array + offset, body) ? body : nullptr;
}

[[nodiscard]] bool owns_player(std::byte* component, std::uint32_t* handle = nullptr) noexcept {
    if (component == nullptr || g_controlledHandle == nullptr) return false;
    std::uint32_t controlled = kInvalidHandle;
    g_controlledHandle(&controlled);
    if (controlled == kInvalidHandle) {
        return false;
    }
    std::uint16_t owner = 0;
    const bool owned = read_at(component + kPhysicsComponentObjectHandle, owner)
                       && (controlled & kHandleIndexMask)
                              == (static_cast<std::uint32_t>(owner) & kHandleIndexMask);
    if (owned && handle != nullptr) *handle = controlled;
    return owned;
}

// reads the native datum pool with the same relocation rules as 464F90
[[nodiscard]] std::byte* resolve_physics_datum(std::uint32_t datum) noexcept {
    if (datum == kInvalidHandle) return nullptr;
    const std::byte* holder{};
    const std::byte* table{};
    if (!read_at(g_nativePlayerLookup.poolPointer, holder) || !holder
        || !read_at(holder, table) || !table) return nullptr;
    const auto shifted = static_cast<std::uint32_t>(static_cast<std::int32_t>(datum) >> 13);
    const auto bank = (((shifted | 0x0FFC0000U) >> 18) & (shifted & 0xFFFFU));
    const std::byte* descriptor = table + static_cast<std::size_t>(bank) * 0x40;
    const std::byte* rows{};
    std::uint32_t stride{};
    std::int32_t mask{};
    std::uint64_t relative{};
    if (!read_at(descriptor + 8, rows) || !rows
        || !read_at(descriptor + 0x30, stride) || stride < 16 || stride > 0x100000
        || !read_at(descriptor + 0x34, mask)) return nullptr;
    const auto raw = reinterpret_cast<std::uintptr_t>(rows)
                     + static_cast<std::size_t>(datum & kHandleIndexMask) * stride;
    if (!read_at(reinterpret_cast<const std::byte*>(raw) + 8, relative)) return nullptr;
    const auto address = raw - (relative & static_cast<std::uint64_t>(static_cast<std::int64_t>(mask)));
    return address >= 0x10000 && address < 0x7FFFFFFFFFFFULL
               ? reinterpret_cast<std::byte*>(address) : nullptr;
}

// a full native unit handle must still name the current datum before and after lookup
[[nodiscard]] bool current_native_unit(std::uint32_t controlled) noexcept {
    const std::byte* units{};
    std::uint32_t stride{}, capacity{}, current{};
    const auto index = controlled & kHandleIndexMask;
    return controlled != kInvalidHandle
           && read_at(g_nativePlayerLookup.unitsPointer, units) && units
           && read_at(g_nativePlayerLookup.unitStride, stride) && stride == 0xE0
           && read_at(g_nativePlayerLookup.unitCapacity, capacity) && capacity == 4096
           && index < capacity && read_at(units + index * stride + 0xC, current)
           && current == controlled;
}

void find_native_player_component() noexcept {
    if (!g_nativePlayerLookup.getter || !g_controlledHandle) return;
    std::uint32_t controlled = kInvalidHandle;
    g_controlledHandle(&controlled);
    if (!current_native_unit(controlled)) return;
    std::uint32_t physicsDatum = kInvalidHandle;
    g_nativePlayerLookup.getter(&physicsDatum, controlled);
    std::byte* const component = resolve_physics_datum(physicsDatum);
    std::uint32_t type{}, componentOwner{}, currentControlled{};
    Vector position{};
    if (!component || !read_at(component + 4, type) || type != 0x80808A0CU
        || !read_at(component + kPhysicsComponentObjectHandle, componentOwner)
        || componentOwner != controlled
        || !owns_player(component, &currentControlled) || currentControlled != controlled
        || !current_native_unit(controlled)) return;
    std::byte* const body = body_of(component);
    if (!body || !read_at(body + kBodyPositionX, position)) return;
    g_playerHandle.store(controlled, std::memory_order_relaxed);
    g_playerComponent.store(component, std::memory_order_relaxed);
}

// loading can retire a matching component before the destination player's body exists
void refresh_cached_component() noexcept {
    std::byte* const component = g_playerComponent.load(std::memory_order_relaxed);
    if (component == nullptr) { find_native_player_component(); return; }
    std::uint32_t controlled = kInvalidHandle;
    if (g_controlledHandle != nullptr) g_controlledHandle(&controlled);
    std::uint32_t type{}, owner{};
    std::byte* const body = body_of(component);
    Vector position{};
    if (controlled != g_playerHandle.load(std::memory_order_relaxed)
        || !read_at(component + 4, type) || type != 0x80808A0CU
        || !read_at(component + kPhysicsComponentObjectHandle, owner) || owner != controlled
        || body == nullptr || !read_at(body + kBodyPositionX, position)) {
        g_playerComponent.store(nullptr, std::memory_order_relaxed);
        g_playerHandle.store(kInvalidHandle, std::memory_order_relaxed);
        find_native_player_component();
    }
}

} // namespace
void publish_targets(ControlledHandle controlled) noexcept {
    AcquireSRWLockExclusive(&g_playerTargetsLock);
    g_controlledHandle = controlled;
    ReleaseSRWLockExclusive(&g_playerTargetsLock);
}
void publish_native_player_lookup(NativePlayerBodyLookup lookup) noexcept {
    AcquireSRWLockExclusive(&g_playerTargetsLock);
    g_nativePlayerLookup = lookup;
    ReleaseSRWLockExclusive(&g_playerTargetsLock);
}
void clear_targets() noexcept {
    AcquireSRWLockExclusive(&g_playerTargetsLock);
    g_controlledHandle = nullptr;
    g_nativePlayerLookup = {};
    g_playerComponent.store(nullptr, std::memory_order_relaxed);
    g_playerHandle.store(kInvalidHandle, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_playerTargetsLock);
}
void* local_player_component() noexcept {
    AcquireSRWLockShared(&g_playerTargetsLock);
    void* result = nullptr;
    // native pool retirement can make a valid getter miss during a load; the observer mutates
    // nothing and retries on its next admitted frame
    __try {
        refresh_cached_component();
        result = g_playerComponent.load(std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_playerComponent.store(nullptr, std::memory_order_relaxed);
        g_playerHandle.store(kInvalidHandle, std::memory_order_relaxed);
    }
    ReleaseSRWLockShared(&g_playerTargetsLock);
    return result;
}
bool owns_local_player(void* component) noexcept {
    AcquireSRWLockShared(&g_playerTargetsLock);
    bool owned = false;
    __try {
        owned = component != nullptr && g_controlledHandle != nullptr
            && owns_player(static_cast<std::byte*>(component));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ReleaseSRWLockShared(&g_playerTargetsLock);
    return owned;
}

bool read_position(void* component, Vector& position) noexcept {
    AcquireSRWLockShared(&g_playerTargetsLock);
    bool valid = false;
    __try {
        auto* const bytes = static_cast<std::byte*>(component);
        std::uint32_t type{}, owner{}, controlled = kInvalidHandle, current = kInvalidHandle;
        if (bytes && g_controlledHandle && read_at(bytes + 4, type) && type == 0x80808A0CU
            && read_at(bytes + kPhysicsComponentObjectHandle, owner)) {
            g_controlledHandle(&controlled);
            if (controlled != kInvalidHandle && owner == controlled) {
                std::byte* const body = body_of(bytes);
                Vector read{};
                if (body && read_at(body + kBodyPositionX, read)) {
                    g_controlledHandle(&current);
                    if (current == controlled) { position = read; valid = true; }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    ReleaseSRWLockShared(&g_playerTargetsLock);
    return valid;
}

} // namespace dawn::client::hooks::teleport
