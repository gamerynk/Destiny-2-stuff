#pragma once

#include <cstdint>

#include "../../middleware/web_service/web_service_envelope.h"

namespace dawn::server::web_service {

void resolve_response_shape(std::uint16_t opcode,
                            middleware::web_service::ResponseShape& shape) noexcept;

} // namespace dawn::server::web_service
