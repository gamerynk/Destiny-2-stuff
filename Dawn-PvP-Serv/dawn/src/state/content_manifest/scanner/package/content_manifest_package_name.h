#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../definition.h"

namespace dawn::state::content_manifest::scanner::package {

enum class NameStatus : std::uint8_t {
    unrecognized,
    excluded,
    invalid,
    recognized,
};

struct ParsedName final {
    std::array<char, kPackageNameCapacity> stem{};
    std::uint16_t stemLength{};
    std::uint16_t identityLength{};
    std::uint16_t packageId{};
    std::uint32_t patchIndex{};
};

[[nodiscard]] NameStatus parse_file_name(std::wstring_view fileName, ParsedName& output) noexcept;

[[nodiscard]] bool parse_stem(std::string_view stem, ParsedName& output) noexcept;

} // namespace dawn::state::content_manifest::scanner::package
