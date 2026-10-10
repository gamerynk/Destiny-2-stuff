#pragma once
#include <cstdint>

namespace dawn::steam::runtime::native_network_diagnostics {
void start() noexcept;
void service(std::uint64_t now) noexcept;
void service_connection_quality(std::uint64_t now) noexcept;
void shutdown() noexcept;
} // namespace dawn::steam::runtime::native_network_diagnostics
