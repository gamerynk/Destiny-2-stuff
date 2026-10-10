#include "gameplay_runtime.h"

#include "association/association_host.h"
#include "dtls/dtls_host.h"
#include "endpoint/gameplay_endpoint.h"
#include "group/group_host.h"
#include "peer/peer_transport.h"

namespace dawn::server::gameplay {

bool initialize() noexcept {
    association::reset();
    dtls::reset();
    peer::reset();
    group::reset();
    return endpoint::initialize();
}

void service(std::uint64_t now) noexcept {
    endpoint::service(now);
    group::service(now);
    peer::service(now);
}

void shutdown() noexcept {
    endpoint::shutdown();
    peer::reset();
    group::reset();
    dtls::reset();
    association::reset();
}

} // namespace dawn::server::gameplay
