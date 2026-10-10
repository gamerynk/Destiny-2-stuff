#include <Windows.h>

#include <imgui.h>

#include "../allocator.h"
#include "internal.h"

namespace dawn::core::ui::memory {
namespace {

ImGuiMemAllocFunc g_previousAllocate{};
ImGuiMemFreeFunc g_previousRelease{};
void* g_previousUserData{};
bool g_installed{};
SRWLOCK g_allocatorLock{SRWLOCK_INIT};

[[nodiscard]] void* allocate_callback(std::size_t size, void* userData) noexcept {
    (void)userData;
    AcquireSRWLockExclusive(&g_allocatorLock);
    void* result = g_installed ? fixed::allocate(size) : nullptr;
    ReleaseSRWLockExclusive(&g_allocatorLock);
    return result;
}

void release_callback(void* pointer, void* userData) noexcept {
    (void)userData;
    AcquireSRWLockExclusive(&g_allocatorLock);
    if (g_installed) {
        fixed::release(pointer);
    }
    ReleaseSRWLockExclusive(&g_allocatorLock);
}

} // namespace

bool initialize() noexcept {
    AcquireSRWLockExclusive(&g_allocatorLock);
    if (g_installed) {
        ReleaseSRWLockExclusive(&g_allocatorLock);
        return true;
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ReleaseSRWLockExclusive(&g_allocatorLock);
        return false;
    }

    ImGui::GetAllocatorFunctions(&g_previousAllocate, &g_previousRelease, &g_previousUserData);
    fixed::reset();
    g_installed = true;
    ImGui::SetAllocatorFunctions(allocate_callback, release_callback, nullptr);
    ReleaseSRWLockExclusive(&g_allocatorLock);
    return true;
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&g_allocatorLock);
    if (!g_installed) {
        ReleaseSRWLockExclusive(&g_allocatorLock);
        return true;
    }
    if (fixed::snapshot().outstandingAllocations != 0) {
        // restoring the callbacks early would send later releases to the wrong allocator
        ReleaseSRWLockExclusive(&g_allocatorLock);
        return false;
    }

    ImGui::SetAllocatorFunctions(g_previousAllocate, g_previousRelease, g_previousUserData);
    fixed::clear();
    g_previousAllocate = nullptr;
    g_previousRelease = nullptr;
    g_previousUserData = nullptr;
    g_installed = false;
    ReleaseSRWLockExclusive(&g_allocatorLock);
    return true;
}

Stats snapshot() noexcept {
    AcquireSRWLockShared(&g_allocatorLock);
    const fixed::ArenaStats arena = fixed::snapshot();
    const Stats result{g_installed,
                       kArenaCapacityBytes,
                       arena.outstandingAllocations,
                       arena.outstandingBytes,
                       arena.highWaterBytes,
                       g_installed ? arena.largestFreeBytes : 0};
    ReleaseSRWLockShared(&g_allocatorLock);
    return result;
}

} // namespace dawn::core::ui::memory
