#include <Windows.h>

#include "../../../targets/game/content.h"
#include "../../diagnostics/content_readiness_report.h"
#include "../internal.h"
#include "../runtime.h"
#include "../worker.h"

namespace dawn::client::content::investment::worker {
namespace {

// no delay between slices; each is bounded, and a 50 ms delay was most of the boot extraction time
constexpr std::uint64_t kRefreshIntervalMilliseconds = 0;

SRWLOCK g_lifecycleLock{SRWLOCK_INIT};
bool g_accepting{};
bool g_complete{};
std::uint64_t g_nextEligible{};

} // namespace

void activate() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    g_accepting = true;
    g_complete = false;
    g_nextEligible = 0;
    ReleaseSRWLockExclusive(&g_lifecycleLock);
}

void service(std::uint64_t nowMilliseconds) noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!g_accepting || g_complete
        || !dawn::client::targets::game::content::is_resolved()
        || nowMilliseconds < g_nextEligible) {
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return;
    }
    g_nextEligible = nowMilliseconds + kRefreshIntervalMilliseconds;

    g_complete = dawn::client::content::investment::refresh();
    dawn::client::content::diagnostics::report_readiness();
    ReleaseSRWLockExclusive(&g_lifecycleLock);
}

void reset() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    g_accepting = false;
    g_complete = false;
    g_nextEligible = 0;
    ReleaseSRWLockExclusive(&g_lifecycleLock);
}

} // namespace dawn::client::content::investment::worker
