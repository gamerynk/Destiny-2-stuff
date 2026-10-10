#pragma once

#include "definition.h"

namespace dawn::server::bap::encrypted {

struct ServiceOutcome;

namespace transactions {

[[nodiscard]] bool commit(ServiceOutcome& outcome, Publication& publication) noexcept;

} // namespace transactions

} // namespace dawn::server::bap::encrypted
