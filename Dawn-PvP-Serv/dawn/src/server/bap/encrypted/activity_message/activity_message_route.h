#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"
#include "../../../../middleware/bap/activity_message/activity_patch_epoch_parser.h"
#include "../../../../middleware/bap/activity_message/request_provenance.h"

namespace dawn::server::bap::encrypted::activity_message {

[[nodiscard]] bool process(std::uint64_t boundSessionId,
                           std::span<const std::byte> requestBody,
                           ActivityPlan& plan,
                           bool& hasTransaction,
                           std::uint64_t boundJoinRevision=0,
                           const middleware::bap::activity_message::patch_epoch::PatchEpoch* expectedEpoch=nullptr,
                           const middleware::bap::activity_message::RequestProvenance& provenance={}) noexcept;

} // namespace dawn::server::bap::encrypted::activity_message
