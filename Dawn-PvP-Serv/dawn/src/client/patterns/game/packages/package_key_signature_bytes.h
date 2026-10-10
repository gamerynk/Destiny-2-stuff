#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "../../registry.h"
#include "../../signature_text.h"

namespace dawn::client::patterns::game::packages {

// loads the package key table into a vector register; carries no key bytes
inline constexpr std::string_view kKeyTableText =
    "0F 10 05 ? ? ? ? 48 8D 64 24 F8 48 89 2C 24 48 8D 2D ? ? ? ? E9";

inline constexpr std::size_t kKeyTablePatternSize = signature_length(kKeyTableText);

extern constinit const std::array<patterns::PatternByte, kKeyTablePatternSize> kKeyTable;

} // namespace dawn::client::patterns::game::packages
