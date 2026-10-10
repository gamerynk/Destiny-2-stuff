#include <Windows.h>
#include <TlHelp32.h>
#include <detours.h>
#include <cassert>
#include <cstdio>

namespace fixture {
int mode{}, snapshots{}, waits{}, updates{}, aborts{}, freeze{};
HANDLE snapshot(DWORD, DWORD) { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(100 + ++snapshots)); }
BOOL first(HANDLE handle, THREADENTRY32* entry) {
    const int pass = static_cast<int>(reinterpret_cast<std::uintptr_t>(handle)) - 100;
    const bool present = mode == 0 || mode == 4 || mode == 5 || pass == 1 || (mode == 6 && pass == 2);
    if (!present) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    entry->th32ThreadID = 10;
    entry->th32OwnerProcessID = mode == 6 && pass == 2 ? 99 : GetCurrentProcessId();
    return TRUE;
}
BOOL next(HANDLE, THREADENTRY32*) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
HANDLE open(DWORD access, BOOL, DWORD) {
    assert((access & (SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION)) ==
           (SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION));
    if (mode == 5 || mode == 6) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<HANDLE>(20);
}
DWORD owner(HANDLE) { return mode == 1 ? 99 : GetCurrentProcessId(); }
DWORD wait(HANDLE, DWORD timeout) {
    assert(timeout == 0); ++waits;
    return mode == 2 || (mode == 3 && waits == 2) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
BOOL close(HANDLE) { return TRUE; }
}
#define CreateToolhelp32Snapshot fixture::snapshot
#define Thread32First fixture::first
#define Thread32Next fixture::next
#define OpenThread fixture::open
#define GetProcessIdOfThread fixture::owner
#define WaitForSingleObject fixture::wait
#define CloseHandle fixture::close
#include "../dawn/src/client/hooking/detour/transaction/detour_thread_transaction.cpp"
#undef CreateToolhelp32Snapshot
#undef Thread32First
#undef Thread32Next
#undef OpenThread
#undef GetProcessIdOfThread
#undef WaitForSingleObject
#undef CloseHandle

extern "C" LONG WINAPI DetourTransactionBegin() { return NO_ERROR; }
extern "C" LONG WINAPI DetourTransactionAbort() { ++fixture::aborts; return NO_ERROR; }
extern "C" LONG WINAPI DetourTransactionCommit() { return NO_ERROR; }
extern "C" LONG WINAPI DetourUpdateThread(HANDLE thread) {
    if (thread == GetCurrentThread()) return NO_ERROR;
    ++fixture::updates;
    return fixture::mode == 3 || fixture::mode == 4 ? ERROR_ACCESS_DENIED : NO_ERROR;
}
extern "C" PVOID WINAPI DetourCodeFromPointer(PVOID code, PVOID*) { return code; }
namespace dawn::client::process::freeze {
void enter_exclusive() noexcept { ++fixture::freeze; }
void leave_exclusive() noexcept { --fixture::freeze; }
}

int main() {
    namespace transaction = dawn::client::hooking::detour::transaction;
    for (int mode = 0; mode <= 6; ++mode) {
        fixture::mode = mode; fixture::snapshots = fixture::waits = fixture::updates = fixture::aborts = 0;
        transaction::Threads threads{};
        const bool succeeded = transaction::begin(threads);
        assert(succeeded == (mode != 4 && mode != 5));
        if (mode == 0) assert(threads.count == 1 && fixture::updates == 1);
        if (mode == 1 || mode == 2 || mode == 6) assert(threads.count == 0 && fixture::updates == 0);
        if (mode == 3) assert(threads.count == 0 && fixture::updates == 1 && fixture::aborts == 1);
        if (mode == 4) assert(transaction::last_failure().error == ERROR_ACCESS_DENIED && fixture::aborts == 1);
        if (mode == 5) assert(transaction::last_failure().error == ERROR_ACCESS_DENIED && fixture::aborts == 1);
        if (succeeded) assert(transaction::commit(threads));
        assert(fixture::freeze == 0);
    }
    std::puts("PASS: current threads enlisted; exited/reused threads skipped; exit race restarts; live denied threads fail");
}
