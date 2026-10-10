#include "detour_thread_transaction.h"

#include <Windows.h>

#include <TlHelp32.h>
#include <detours.h>

#include "../../../process/freeze/client_process_freeze.h"

namespace dawn::client::hooking::detour::transaction {
namespace {
thread_local Failure g_failure{};
void fail(const char* stage, LONG error, const Threads& threads, DWORD threadId = 0) noexcept {
    g_failure = {stage, error, threads.count, threadId};
}

constexpr std::size_t kProtectedCodeLimit = 64;

struct CodeRange {
    DWORD64 begin{};
    DWORD64 end{};
};

void close_threads(Threads& threads) noexcept {
    for (std::size_t index = 0; index < threads.count; ++index) {
        CloseHandle(threads.handles[index]);
    }
    threads = {};
}

[[nodiscard]] bool contains(const Threads& threads, DWORD threadId) noexcept {
    for (std::size_t index = 0; index < threads.count; ++index) {
        if (threads.ids[index] == threadId) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool still_owned(DWORD threadId, DWORD processId) noexcept {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL available = Thread32First(snapshot, &entry);
    while (available != FALSE) {
        if (entry.th32ThreadID == threadId) {
            const bool owned = entry.th32OwnerProcessID == processId;
            CloseHandle(snapshot);
            return owned;
        }
        available = Thread32Next(snapshot, &entry);
    }
    const bool unknown = GetLastError() != ERROR_NO_MORE_FILES;
    CloseHandle(snapshot);
    return unknown;
}

[[nodiscard]] bool enlist_snapshot(Threads& threads, bool& foundUnseen,
                                   bool& exitedDuringUpdate) noexcept {
    foundUnseen = false;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        fail("thread_snapshot", static_cast<LONG>(GetLastError()), threads);
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
        const bool needsEnlistment =
            entry.th32ThreadID != currentThreadId && !contains(threads, entry.th32ThreadID);
        if (belongsToProcess && needsEnlistment) {
            foundUnseen = true;
            if (threads.count == threads.handles.size()) {
                fail("thread_capacity", ERROR_INSUFFICIENT_BUFFER, threads, entry.th32ThreadID);
                succeeded = false;
                break;
            }

            const HANDLE thread =
                OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT
                               | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                           FALSE,
                           entry.th32ThreadID);
            if (thread == nullptr) {
                const DWORD error = GetLastError();
                // ids may be reused between Toolhelp and OpenThread; only a fresh census proving absence or
                // another owner can forgive denied access
                if (error != ERROR_INVALID_PARAMETER
                    && still_owned(entry.th32ThreadID, processId)) {
                    fail("open_thread", static_cast<LONG>(error), threads,
                         entry.th32ThreadID);
                    succeeded = false;
                }
            } else {
                const DWORD owner = GetProcessIdOfThread(thread);
                const DWORD state = WaitForSingleObject(thread, 0);
                if (owner != processId || state != WAIT_TIMEOUT) {
                    const DWORD error = GetLastError();
                    CloseHandle(thread);
                    if (owner == 0 || state == WAIT_FAILED) {
                        fail("thread_identity", static_cast<LONG>(error), threads,
                             entry.th32ThreadID);
                        succeeded = false;
                    }
                    available = Thread32Next(snapshot, &entry);
                    continue;
                }
                const LONG update = DetourUpdateThread(thread);
                if (update != NO_ERROR) {
                    // SuspendThread can report access denied after the thread exits; Detours has latched its error,
                    // so abort the attempt and retry only when this held object is now signalled
                    exitedDuringUpdate = WaitForSingleObject(thread, 0) == WAIT_OBJECT_0;
                    fail(exitedDuringUpdate ? "update_thread_exited" : "update_thread",
                         update, threads, entry.th32ThreadID);
                    CloseHandle(thread);
                    succeeded = false;
                } else {
                    threads.handles[threads.count] = thread;
                    threads.ids[threads.count] = entry.th32ThreadID;
                    ++threads.count;
                }
            }
        }
        available = Thread32Next(snapshot, &entry);
    }

    if (succeeded && available == FALSE && GetLastError() != ERROR_NO_MORE_FILES) {
        fail("thread_enumeration", static_cast<LONG>(GetLastError()), threads);
        succeeded = false;
    }
    CloseHandle(snapshot);
    return succeeded;
}

[[nodiscard]] bool enlist_until_stable(Threads& threads, bool& exitedDuringUpdate) noexcept {
    bool foundUnseen{};
    do {
        if (!enlist_snapshot(threads, foundUnseen, exitedDuringUpdate)) {
            return false;
        }
    } while (foundUnseen);
    return true;
}

[[nodiscard]] bool is_executable(DWORD protection) noexcept {
    constexpr DWORD kPageTypeMask = 0xFF;
    switch (protection & kPageTypeMask) {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool resolve_range(const ProtectedCodeEntry& entry, CodeRange& range) noexcept {
    range = {};
    if (entry.address == nullptr) {
        return false;
    }

    MEMORY_BASIC_INFORMATION entryMemory{};
    if (VirtualQuery(entry.address, &entryMemory, sizeof(entryMemory)) != sizeof(entryMemory)
        || entryMemory.State != MEM_COMMIT || !is_executable(entryMemory.Protect)) {
        return false;
    }

    void* const code = DetourCodeFromPointer(entry.address, nullptr);
    MEMORY_BASIC_INFORMATION codeMemory{};
    if (code == nullptr || VirtualQuery(code, &codeMemory, sizeof(codeMemory)) != sizeof(codeMemory)
        || codeMemory.State != MEM_COMMIT || !is_executable(codeMemory.Protect)) {
        return false;
    }

    const DWORD64 codeAddress = reinterpret_cast<DWORD64>(code);
    DWORD64 imageBase{};
    const RUNTIME_FUNCTION* function = RtlLookupFunctionEntry(codeAddress, &imageBase, nullptr);
    if (function == nullptr) {
        return false;
    }

    range = {imageBase + function->BeginAddress, imageBase + function->EndAddress};
    return range.begin < range.end && codeAddress >= range.begin && codeAddress < range.end;
}

} // namespace

Failure last_failure() noexcept { return g_failure; }

bool begin(Threads& threads) noexcept {
    threads = {};
    g_failure = {};
    // Detours suspends these threads at commit; another suspender running at the same time
    // would freeze this thread and neither side could finish
    process::freeze::enter_exclusive();
    constexpr unsigned kExitedThreadAttempts = 8;
    for (unsigned attempt = 0; attempt < kExitedThreadAttempts; ++attempt) {
        g_failure = {};
        const LONG beginError = DetourTransactionBegin();
        if (beginError != NO_ERROR) {
            fail("transaction_begin", beginError, threads);
            process::freeze::leave_exclusive();
            return false;
        }
        const LONG currentUpdate = DetourUpdateThread(GetCurrentThread());
        if (currentUpdate != NO_ERROR) fail("update_current_thread", currentUpdate, threads,
                                          GetCurrentThreadId());
        bool exitedDuringUpdate = false;
        if (currentUpdate != NO_ERROR || !enlist_until_stable(threads, exitedDuringUpdate)) {
            (void)DetourTransactionAbort();
            close_threads(threads);
            if (exitedDuringUpdate && attempt + 1 < kExitedThreadAttempts) continue;
            process::freeze::leave_exclusive();
            return false;
        }
        g_failure.threadCount = threads.count;
        return true;
    }
    process::freeze::leave_exclusive();
    return false;
}

bool abort(Threads& threads) noexcept {
    const bool aborted = DetourTransactionAbort() == NO_ERROR;
    close_threads(threads);
    process::freeze::leave_exclusive();
    return aborted;
}

bool commit(Threads& threads) noexcept {
    const LONG error = DetourTransactionCommit();
    const bool committed = error == NO_ERROR;
    if (!committed) fail("transaction_commit", error, threads);
    close_threads(threads);
    process::freeze::leave_exclusive();
    return committed;
}

InspectionResult inspect(const Threads& threads,
                         std::span<const ProtectedCodeEntry> entries) noexcept {
    if (entries.empty() || entries.size() > kProtectedCodeLimit) {
        return InspectionResult::failed;
    }

    std::array<CodeRange, kProtectedCodeLimit> ranges{};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!resolve_range(entries[index], ranges[index])) {
            return InspectionResult::failed;
        }
    }

    for (std::size_t threadIndex = 0; threadIndex < threads.count; ++threadIndex) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(threads.handles[threadIndex], &context) == FALSE) {
            return InspectionResult::failed;
        }
        for (std::size_t rangeIndex = 0; rangeIndex < entries.size(); ++rangeIndex) {
            const CodeRange range = ranges[rangeIndex];
            if (context.Rip >= range.begin && context.Rip < range.end) {
                return InspectionResult::protectedCodeActive;
            }
        }
    }
    return InspectionResult::clear;
}

} // namespace dawn::client::hooking::detour::transaction
