#pragma once

namespace dawn::client::runtime::network_route_policy {
[[nodiscard]] bool native_requested() noexcept;
[[nodiscard]] bool prepare() noexcept;
[[nodiscard]] bool initialize() noexcept;
[[nodiscard]] bool ready() noexcept;
void report() noexcept;
}
