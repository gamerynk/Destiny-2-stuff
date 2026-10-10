#pragma once

#include "../encoding/bit_writer.h"
#include "web_service_envelope.h"

namespace dawn::middleware::web_service::status {

[[nodiscard]] bool write_fields(encoding::bits::Writer& writer,
                                ResponseShape shape,
                                const StatusResponse& response) noexcept;

} // namespace dawn::middleware::web_service::status
