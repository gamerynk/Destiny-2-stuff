#pragma once

#include "../platform/sdk.h"

namespace dawn::client::hooks::egress::dns {

DNS_STATUS WINAPI query_ex(PDNS_QUERY_REQUEST request,
                           PDNS_QUERY_RESULT results,
                           PDNS_QUERY_CANCEL cancel) noexcept;

} // namespace dawn::client::hooks::egress::dns
