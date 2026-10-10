#pragma once

#include "policy.h"

namespace dawn::client::hooks::egress::policy {

void log_decision(SocketOperation operation, bool targetsRedirect, bool allowed) noexcept;

} // namespace dawn::client::hooks::egress::policy
