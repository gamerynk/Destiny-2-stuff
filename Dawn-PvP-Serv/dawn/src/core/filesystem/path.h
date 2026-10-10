#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::core::path {

inline constexpr std::size_t kExtendedPathCapacity = 32768;

struct Buffer {
    std::array<wchar_t, kExtendedPathCapacity> chars{};
    std::size_t length{};
};

[[nodiscard]] bool assign(Buffer& path, std::wstring_view value) noexcept;

[[nodiscard]] bool module_directory(void* module, Buffer& output) noexcept;

[[nodiscard]] bool artifact_directory(void* module, Buffer& output) noexcept;

// resolves <dir holding steam_api64.dll>\Restoration\<relative>, creating directories;
// finds the module from its own address so nothing threads a handle down
[[nodiscard]] bool artifact_file(std::wstring_view relative, Buffer& output) noexcept;

// opened fresh each call so edits apply without a relaunch; a file too large for the
// storage is refused, never truncated into a half-rule that parses as something else
[[nodiscard]] bool read_artifact_text(std::wstring_view relative, std::span<char> text) noexcept;

[[nodiscard]] bool append(Buffer& path, std::wstring_view suffix) noexcept;

} // namespace dawn::core::path
