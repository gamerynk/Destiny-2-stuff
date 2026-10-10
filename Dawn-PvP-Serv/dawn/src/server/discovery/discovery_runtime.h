#pragma once

namespace dawn::server::discovery {

[[nodiscard]] bool initialize() noexcept;

void service() noexcept;

void shutdown() noexcept;

[[nodiscard]] bool ready() noexcept;

} // namespace dawn::server::discovery
