#include "settings_runtime.h"
#include "../../runtime/storage/internal.h"
namespace dawn::state::account::settings {
bool save(std::uint64_t accountSoid, const AccountSettings& before,
          const AccountSettings& after, bool& changed) noexcept {
    changed = false;
    if (accountSoid == 0 || !valid(after)) return false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const auto& current = runtime::storage::account();
    bool result = current.primarySoid == accountSoid && current.settings == before;
    if (result && before != after) {
        auto candidate = current;
        candidate.settings = after;
        result = runtime::storage::publish_account_locked(candidate);
        changed = result;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return result;
}
}
