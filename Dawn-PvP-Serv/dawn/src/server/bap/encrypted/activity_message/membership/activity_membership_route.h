#pragma once

#include "../../../../../middleware/bap/activity_message/definition.h"
#include "../definition.h"
#include "middleware/bap/activity_message/activity_client_identity_parser.h"

namespace dawn::server::bap::encrypted::activity_message::membership {
[[nodiscard]] bool authorize_reservations(
    std::span<const middleware::bap::activity_message::client_identity::ClientIdentity> peers,
    std::uint64_t socialSession=0) noexcept;
[[nodiscard]] bool authorize_social_startup_reservations(
    std::span<const middleware::bap::activity_message::client_identity::ClientIdentity> peers) noexcept;
[[nodiscard]] bool prepare_reservations(const middleware::bap::activity_message::Request& request,
                                        ActivityPlan& plan) noexcept;

[[nodiscard]] bool prepare_identity(const middleware::bap::activity_message::Request& request,
                                    ActivityPlan& plan) noexcept;

[[nodiscard]] bool prepare_authoritative(const middleware::bap::activity_message::Request& request,
                                         ActivityPlan& plan) noexcept;

[[nodiscard]] bool prepare_refresh(const middleware::bap::activity_message::Request& request,
                                   ActivityPlan& plan) noexcept;

[[nodiscard]] bool
prepare_acknowledgement(const middleware::bap::activity_message::Request& request,
                        ActivityPlan& plan) noexcept;

} // namespace dawn::server::bap::encrypted::activity_message::membership
