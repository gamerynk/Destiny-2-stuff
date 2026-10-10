#include "path.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace dawn::core::path {
namespace {

// one owned folder keeps generated files from piling up beside the game binaries
constexpr std::wstring_view kArtifactDirectorySuffix = L"Restoration";

} // namespace

bool assign(Buffer& path, std::wstring_view value) noexcept {
    if (value.size() >= path.chars.size()) {
        return false;
    }
    path = {};
    std::memcpy(path.chars.data(), value.data(), value.size() * sizeof(wchar_t));
    path.length = value.size();
    path.chars[path.length] = L'\0';
    return true;
}

bool module_directory(void* module, Buffer& output) noexcept {
    if (module == nullptr) {
        return false;
    }

    const DWORD copied = GetModuleFileNameW(
        static_cast<HMODULE>(module), output.chars.data(), static_cast<DWORD>(output.chars.size()));
    if (copied == 0 || copied == output.chars.size()) {
        return false;
    }

    output.length = copied;
    while (output.length != 0 && output.chars[output.length - 1] != L'\\') {
        --output.length;
    }
    if (output.length == 0) {
        return false;
    }
    output.chars[output.length] = L'\0';
    return true;
}

bool artifact_directory(void* module, Buffer& output) noexcept {
    if (!module_directory(module, output) || !append(output, kArtifactDirectorySuffix)) {
        return false;
    }
    if (CreateDirectoryW(output.chars.data(), nullptr) != FALSE) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    // ERROR_ALREADY_EXISTS also covers files, so check the existing object is a directory
    const DWORD attributes = GetFileAttributesW(output.chars.data());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool artifact_file(std::wstring_view relative, Buffer& output) noexcept {
    HMODULE self{};
    // use this function's own address so it names the DLL, not the host exe (game in the
    // install root, this module in bin\x64)
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&artifact_file),
                           &self)
            == FALSE
        || self == nullptr) {
        return false;
    }
    if (!artifact_directory(self, output)) {
        return false;
    }
    // create each directory named before the file so exports\x.txt works on a fresh install
    std::size_t start = 0;
    for (std::size_t index = 0; index < relative.size(); ++index) {
        if (relative[index] != L'\\') {
            continue;
        }
        if (!append(output, L"\\") || !append(output, relative.substr(start, index - start))) {
            return false;
        }
        if (CreateDirectoryW(output.chars.data(), nullptr) == FALSE
            && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
        start = index + 1;
    }
    return append(output, L"\\") && append(output, relative.substr(start));
}

bool read_artifact_text(std::wstring_view relative, std::span<char> text) noexcept {
    if (text.empty()) {
        return false;
    }
    text[0] = '\0';
    Buffer file{};
    if (!artifact_file(relative, file)) {
        return false;
    }
    const HANDLE handle = CreateFileW(file.chars.data(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    // measure first and refuse a file too big for the storage; half a rule would parse as a
    // whole one. One byte is kept for the terminator, so an exact fit still reads whole
    LARGE_INTEGER size{};
    DWORD read = 0;
    const bool measured = GetFileSizeEx(handle, &size) != FALSE;
    const bool fits = measured && size.QuadPart >= 0
                      && static_cast<std::uint64_t>(size.QuadPart) < text.size();
    const bool ok = fits
                    && ReadFile(handle,
                                text.data(),
                                static_cast<DWORD>(text.size() - 1),
                                &read,
                                nullptr)
                           != FALSE;
    (void)CloseHandle(handle);
    if (!ok || read == 0) {
        return false;
    }
    text[read] = '\0';
    return true;
}

bool append(Buffer& path, std::wstring_view suffix) noexcept {
    if (path.length + suffix.size() >= path.chars.size()) {
        return false;
    }
    std::memcpy(path.chars.data() + path.length, suffix.data(), suffix.size() * sizeof(wchar_t));
    path.length += suffix.size();
    path.chars[path.length] = L'\0';
    return true;
}

} // namespace dawn::core::path
