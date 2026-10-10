#pragma once

#include "../definition.h"

namespace dawn::server::bap::encrypted::queuez::staging {

[[nodiscard]] bool same_resident(const ResidentObject& left, const ResidentObject& right) noexcept;

[[nodiscard]] bool same_state(const SessionState& left, const SessionState& right) noexcept;

} // namespace dawn::server::bap::encrypted::queuez::staging
