#pragma once

#include <cstddef>
#include <span>

#include "registry.h"

namespace dawn::client::patterns::steam {

enum class Id : std::size_t {
    authenticationStatus,
    setCertificate,
    count,
};

[[nodiscard]] std::span<const patterns::Pattern> definitions() noexcept;

} // namespace dawn::client::patterns::steam
