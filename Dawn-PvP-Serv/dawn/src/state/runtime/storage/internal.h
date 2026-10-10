#pragma once

#include <Windows.h>

#include "../state.h"

namespace dawn::state::runtime::storage {

extern State g_state;
extern SRWLOCK g_stateLock;
extern AccountPersistence g_accountPersistence;
extern ActivityPersistence g_activityPersistence;

[[nodiscard]] AccountState& account() noexcept;
[[nodiscard]] activity::ActivityState& activity() noexcept;
[[nodiscard]] AccountPersistence account_persistence() noexcept;
[[nodiscard]] ActivityPersistence activity_persistence() noexcept;

[[nodiscard]] bool publish_account_locked(const AccountState& account) noexcept;

[[nodiscard]] bool publish_activity_locked(const activity::ActivityState& activity) noexcept;

} // namespace dawn::state::runtime::storage
