#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::progressions {

inline constexpr std::size_t kDefinitionCapacity = 256;

enum class Scope : std::uint8_t {
    account = 0,
    character = 1,
    unreplicated = 2,
};

struct Definition {
    std::uint16_t definitionIndex{};
    Scope scope{Scope::unreplicated};
};

} // namespace dawn::state::build_data::progressions
