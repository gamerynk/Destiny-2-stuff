#pragma once

#include <cstdint>

#include "../handles/handle_resolver.h"

namespace dawn::client::content::investment {

struct Source {
    std::uint32_t investmentGlobalsTag{};
    handles::Source handles{};
};

} // namespace dawn::client::content::investment
