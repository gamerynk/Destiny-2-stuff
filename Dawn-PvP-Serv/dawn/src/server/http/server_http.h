#pragma once

#include "../../client/network/consumer.h"

namespace dawn::server::http {

using ExtensionConsumer = bool (*)(const client::network::HttpRequest&,
                                   client::network::HttpResponse&) noexcept;

[[nodiscard]] bool register_extension_consumer(ExtensionConsumer consumer) noexcept;

void unregister_extension_consumer(ExtensionConsumer consumer) noexcept;

[[nodiscard]] bool consume(const client::network::HttpRequest& request,
                           client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool consume_public(const client::network::HttpRequest& request,
                                  client::network::HttpResponse& response) noexcept;

} // namespace dawn::server::http
