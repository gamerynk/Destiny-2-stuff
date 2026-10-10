#include <Windows.h>

#include <limits>

#include "../../../../core/filesystem/path.h"
#include "internal.h"

namespace dawn::middleware::content::packages::named_tags {
namespace {

constexpr std::wstring_view kPackageWildcard = L"*.pkg";
constexpr std::wstring_view kPathSeparator = L"\\";

[[nodiscard]] bool
read_file(void* context, std::uint64_t offset, std::span<std::byte> output) noexcept {
    if (context == nullptr || output.empty()
        || offset > static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)())
        || output.size() > (std::numeric_limits<DWORD>::max)()) {
        return false;
    }
    const HANDLE file = *static_cast<const HANDLE*>(context);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE
           && ReadFile(file, output.data(), static_cast<DWORD>(output.size()), &read, nullptr)
                  != FALSE
           && read == static_cast<DWORD>(output.size());
}

} // namespace

bool extract_file(const wchar_t* path, Visitor visitor, void* context, Result& result) noexcept {
    result = {};
    if (path == nullptr) {
        return false;
    }
    HANDLE file = CreateFileW(path,
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    const bool sized = GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0;
    const Reader reader{
        &file,
        sized ? static_cast<std::uint64_t>(size.QuadPart) : 0,
        &read_file,
    };
    const bool extracted = sized && extract(reader, visitor, context, result);
    const bool closed = CloseHandle(file) != FALSE;
    return extracted && closed;
}

bool extract_directory(std::wstring_view directory,
                       Visitor visitor,
                       void* context,
                       DirectoryResult& result) noexcept {
    PatchStamp stamp{};
    result = {};
    if (directory.empty() || visitor == nullptr) {
        return false;
    }
    core::path::Buffer base;
    if (!core::path::assign(base, directory)
        || (base.chars[base.length - 1] != L'\\' && !core::path::append(base, kPathSeparator))) {
        return false;
    }
    core::path::Buffer search = base;
    if (!core::path::append(search, kPackageWildcard)) {
        return false;
    }

    WIN32_FIND_DATAW found{};
    const HANDLE enumeration = FindFirstFileW(search.chars.data(), &found);
    if (enumeration == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool complete = true;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        core::path::Buffer file = base;
        Result fileResult{};
        stamp.inner = visitor;
        stamp.context = context;
        stamp.patchIndex = leaf_patch_index(found.cFileName);
        if (!core::path::append(file, found.cFileName)
            || !extract_file(file.chars.data(), &stamp_patch, &stamp, fileResult)) {
            complete = false;
            break;
        }
        ++result.files;
        if (fileResult.entries > (std::numeric_limits<std::size_t>::max)() - result.entries) {
            complete = false;
            break;
        }
        result.entries += fileResult.entries;
    } while (FindNextFileW(enumeration, &found) != FALSE);

    const DWORD enumerationError = GetLastError();
    const bool closed = FindClose(enumeration) != FALSE;
    return complete && enumerationError == ERROR_NO_MORE_FILES && closed;
}

} // namespace dawn::middleware::content::packages::named_tags
