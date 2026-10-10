#include "activity_destination_snapshot.h"

#include <Windows.h>

#include "../../runtime/storage/internal.h"
#include "../definition.h"
#include "activity_destination_validation.h"

namespace dawn::state::activity::destination {

bool snapshot(std::uint64_t sessionId, DestinationSelection& output) noexcept {
    output = {};
    if (sessionId == kAbsentSessionId) {
        return false;
    }

    DestinationSelection selected{};
    bool found = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const std::uint64_t ownerAccountSoid = runtime::storage::account().primarySoid;
    for (const SessionRecord& record : runtime::storage::activity().sessions) {
        if (record.occupied && record.ownerAccountSoid == ownerAccountSoid
            && record.sessionId == sessionId) {
            selected = record.destination;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (!found || !valid(selected)) {
        return false;
    }
    output = selected;
    return true;
}

bool snapshot_for_runtime(const AccountRuntimeContext& context,
                           std::uint64_t authenticatedAccountSoid,
                           std::uint64_t sessionId,
                           DestinationSelection& output) noexcept {
    output = {};
    if (authenticatedAccountSoid == 0 || sessionId == kAbsentSessionId
        || ((context.account == nullptr) != (context.activity == nullptr))) {
        return false;
    }
    DestinationSelection selected{};
    bool found = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const AccountState& account = context.account != nullptr
                                     ? *context.account : runtime::storage::g_state.account;
    const ActivityState& activity = context.activity != nullptr
                                       ? *context.activity : runtime::storage::g_state.activity;
    if (account.primarySoid == authenticatedAccountSoid) {
        for (const SessionRecord& record : activity.sessions) {
            if (record.occupied && record.ownerAccountSoid == authenticatedAccountSoid
                && record.sessionId == sessionId) {
                selected = record.destination;
                found = true;
                break;
            }
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (!found || !valid(selected)) return false;
    output = selected;
    return true;
}

} // namespace dawn::state::activity::destination
