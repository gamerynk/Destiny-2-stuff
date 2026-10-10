#pragma once

#include <cstddef>
#include <span>

#include "../../../internal.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] bool consume_activity_keepalive(Session& session,
                                              Scratch& scratch,
                                              std::span<std::byte> response,
                                              std::size_t& written,
                                              bool& touchesScratch) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
