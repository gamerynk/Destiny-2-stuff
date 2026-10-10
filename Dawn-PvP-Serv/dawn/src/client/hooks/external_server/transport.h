#pragma once

#include "../../network/consumer.h"

namespace dawn::client::hooks::external_server {

[[nodiscard]] bool consume(const client::network::HttpRequest& request,
                           client::network::HttpResponse& response) noexcept;

} // namespace dawn::client::hooks::external_server
