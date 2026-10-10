#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawn::middleware::content::packages::named_tags {

inline constexpr std::size_t kNameCapacity = 128;

struct Entry {
    std::array<char, kNameCapacity> name{};
    std::size_t nameLength{};
    std::uint32_t tag{};
    std::uint32_t classId{};
    // patch index of the package file this entry came from; a name appears once per patch
    // with a different tag, so the highest patch holds the current tag
    std::uint32_t patchIndex{};
};

struct Result {
    std::size_t entries{};
};

struct DirectoryResult {
    std::size_t files{};
    std::size_t entries{};
};

using Visitor = bool (*)(void* context, const Entry& entry) noexcept;

[[nodiscard]] bool
extract_file(const wchar_t* path, Visitor visitor, void* context, Result& result) noexcept;

[[nodiscard]] bool extract_directory(std::wstring_view directory,
                                     Visitor visitor,
                                     void* context,
                                     DirectoryResult& result) noexcept;

} // namespace dawn::middleware::content::packages::named_tags
