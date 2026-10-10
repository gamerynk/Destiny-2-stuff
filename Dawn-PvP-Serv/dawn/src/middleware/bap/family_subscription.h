#pragma once

#include <cstddef>
#include <span>

#include "../queuez/subscription.h"

namespace dawn::middleware::bap::family_subscription {

[[nodiscard]] bool parse(std::span<const std::byte> input,
                         queuez::Subscription& subscription) noexcept;

} // namespace dawn::middleware::bap::family_subscription
