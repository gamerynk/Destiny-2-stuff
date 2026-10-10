#pragma once

#include <cstddef>
#include <span>

#include "../patterns/registry.h"

namespace dawn::client::targets::steam {

struct Targets {
    std::byte* authenticationStatus{};
    std::byte* setCertificate{};
};

[[nodiscard]] bool resolve(std::span<const patterns::ImageRange> image) noexcept;

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

} // namespace dawn::client::targets::steam
