#pragma once

#include "../../../../../middleware/bap/activity_message/definition.h"
#include "../definition.h"

namespace dawn::server::bap::encrypted::activity_message::patch_epoch {

[[nodiscard]] bool prepare(std::uint64_t sessionId,
                           const middleware::bap::activity_message::Request& request,
                           ActivityPlan& plan) noexcept;

} // namespace dawn::server::bap::encrypted::activity_message::patch_epoch
