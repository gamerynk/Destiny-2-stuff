#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../internal.h"
#include "../../activity_message/definition.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool
append_membership_notification(Scratch& scratch,
                               const activity_message::ActivityPlan& activity,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::array<std::byte, state::kBapNonceSize>& nonce,
                               std::span<std::byte> response,
                               std::size_t& written,
                               bool includeCitizenAdvertisement = true) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
