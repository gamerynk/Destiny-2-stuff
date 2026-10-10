#include "client_process_freeze.h"

#include <Windows.h>

#include <TlHelp32.h>

#include "../../../core/logging/log.h"
#include "../../diagnostics/module_range.h"

namespace dawn::client::process::freeze {
namespace {

constexpr std::size_t kAttemptLimit = 8;
// one millisecond lets a rejected thread leave the code that rejected it
constexpr DWORD kRetryMilliseconds = 1;
constexpr DWORD kSuspendFailed = static_cast<DWORD>(-1);
constexpr LONG kStatusSuccess = 0;
constexpr ULONG kBlockingLoaderLock = 0;

using LockLoaderFunction = LONG(NTAPI*)(ULONG flags, ULONG* state, ULONG_PTR* cookie);
using UnlockLoaderFunction = LONG(NTAPI*)(ULONG flags, ULONG_PTR cookie);

struct LoaderLock {
    LockLoaderFunction lock{};
    UnlockLoaderFunction unlock{};
};

SRWLOCK g_exclusiveLock{SRWLOCK_INIT};

[[nodiscard]] LoaderLock resolve_loader() noexcept {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return {};
    }
    return {reinterpret_cast<LockLoaderFunction>(GetProcAddress(ntdll, "LdrLockLoaderLock")),
            reinterpret_cast<UnlockLoaderFunction>(GetProcAddress(ntdll, "LdrUnlockLoaderLock"))};
}

[[nodiscard]] const LoaderLock& loader() noexcept {
    static const LoaderLock resolved = resolve_loader();
    return resolved;
}

[[nodiscard]] diagnostics::ModuleRange own_range() noexcept {
    const DWORD flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    HMODULE module = nullptr;
    if (GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&own_range), &module) == FALSE) {
        return {};
    }
    diagnostics::ModuleRange range{};
    (void)diagnostics::module_range(module, range);
    return range;
}

// the loader takes its own lock before the heap, so take them in that order; both are
// recursive, so this thread can still load modules and allocate while holding them
[[nodiscard]] bool take_locks(Held& held) noexcept {
    ULONG state = 0;
    ULONG_PTR cookie = 0;
    if (loader().lock(kBlockingLoaderLock, &state, &cookie) != kStatusSuccess) {
        return false;
    }
    held.loaderCookie = cookie;
    held.loaderLocked = true;
    if (HeapLock(GetProcessHeap()) == FALSE) {
        held.loaderLocked = false;
        (void)loader().unlock(kBlockingLoaderLock, cookie);
        return false;
    }
    held.heapLocked = true;
    return true;
}

void drop_locks(Held& held) noexcept {
    if (held.heapLocked) {
        held.heapLocked = false;
        (void)HeapUnlock(GetProcessHeap());
    }
    if (held.loaderLocked) {
        held.loaderLocked = false;
        (void)loader().unlock(kBlockingLoaderLock, held.loaderCookie);
    }
    held.loaderCookie = 0;
}

void resume_threads(Held& held) noexcept {
    for (std::size_t index = 0; index < held.count; ++index) {
        (void)ResumeThread(held.handles[index]);
        CloseHandle(held.handles[index]);
    }
    held.handles = {};
    held.ids = {};
    held.count = 0;
}

[[nodiscard]] bool contains(const Held& held, DWORD threadId) noexcept {
    for (std::size_t index = 0; index < held.count; ++index) {
        if (held.ids[index] == threadId) {
            return true;
        }
    }
    return false;
}

// a thread stopped inside Dawn may own a Dawn lock the caller takes next, so keep only
// threads that stopped outside Dawn code
[[nodiscard]] bool
hold_thread(Held& held, const diagnostics::ModuleRange& range, DWORD threadId) noexcept {
    const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (thread == nullptr) {
        // a thread that exited since the snapshot is absent from the next one
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }
    if (SuspendThread(thread) == kSuspendFailed) {
        CloseHandle(thread);
        return false;
    }

    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(thread, &context) == FALSE || diagnostics::contains(range, context.Rip)) {
        (void)ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }
    held.handles[held.count] = thread;
    held.ids[held.count] = threadId;
    ++held.count;
    return true;
}

[[nodiscard]] bool
hold_snapshot(Held& held, const diagnostics::ModuleRange& range, bool& foundUnseen) noexcept {
    foundUnseen = false;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL available = Thread32First(snapshot, &entry);
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    bool succeeded = true;
    while (available != FALSE && succeeded) {
        const bool belongsToProcess = entry.th32OwnerProcessID == processId;
        const bool needsHold =
            entry.th32ThreadID != currentThreadId && !contains(held, entry.th32ThreadID);
        if (belongsToProcess && needsHold) {
            foundUnseen = true;
            succeeded =
                held.count < held.handles.size() && hold_thread(held, range, entry.th32ThreadID);
        }
        available = Thread32Next(snapshot, &entry);
    }

    if (succeeded && available == FALSE && GetLastError() != ERROR_NO_MORE_FILES) {
        succeeded = false;
    }
    CloseHandle(snapshot);
    return succeeded;
}

[[nodiscard]] bool hold_once(Held& held, const diagnostics::ModuleRange& range) noexcept {
    bool foundUnseen = true;
    while (foundUnseen) {
        if (!hold_snapshot(held, range, foundUnseen)) {
            return false;
        }
        // earlier handles stay suspended while a later pass finds newly created threads
    }
    // a held thread inside a sink stopped in the OS, so no address test sees it
    return !core::log::writers_active();
}

} // namespace

bool hold(Held& held) noexcept {
    held = {};
    const diagnostics::ModuleRange range = own_range();
    if (range.end == 0 || loader().lock == nullptr || loader().unlock == nullptr) {
        return false;
    }

    enter_exclusive();
    for (std::size_t attempt = 0; attempt < kAttemptLimit; ++attempt) {
        if (!take_locks(held)) {
            break;
        }
        if (hold_once(held, range)) {
            return true;
        }
        // a retry must run with the process locks free, or a resumed thread can't make the
        // progress that would let the next pass accept it
        resume_threads(held);
        drop_locks(held);
        Sleep(kRetryMilliseconds);
    }
    leave_exclusive();
    return false;
}

void release(Held& held) noexcept {
    // both locks are taken together, so either flag marks a hold that claimed the process
    const bool claimed = held.loaderLocked || held.heapLocked;
    resume_threads(held);
    drop_locks(held);
    if (claimed) {
        leave_exclusive();
    }
}

void enter_exclusive() noexcept {
    AcquireSRWLockExclusive(&g_exclusiveLock);
}

void leave_exclusive() noexcept {
    ReleaseSRWLockExclusive(&g_exclusiveLock);
}

} // namespace dawn::client::process::freeze
