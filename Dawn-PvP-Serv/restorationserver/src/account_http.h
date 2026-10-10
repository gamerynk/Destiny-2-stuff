#pragma once

#include "client/network/consumer.h"

namespace restoration::account_http {

[[nodiscard]] bool consume(const dawn::client::network::HttpRequest& request,
                           dawn::client::network::HttpResponse& response) noexcept;

} // namespace restoration::account_http
