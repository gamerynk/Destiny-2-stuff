#pragma once
#include <cstddef>

namespace dawn::server::gameplay {
inline constexpr std::size_t kTransportPeerCapacity = 64;
inline constexpr std::size_t kTransportDtlsCapacity = kTransportPeerCapacity * 4;
}
