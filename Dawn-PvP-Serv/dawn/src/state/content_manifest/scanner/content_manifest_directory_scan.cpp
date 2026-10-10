#include <Windows.h>

#include <algorithm>
#include <array>
#include <string_view>

#include "../../../core/filesystem/path.h"
#include "../fingerprint/content_manifest_fingerprint.h"
#include "internal.h"

namespace dawn::state::content_manifest::scanner {
namespace {

constexpr char kDomainTerminator = '\0';
constexpr char kDirectoryDomainVersion = '\1';
constexpr std::array<char, 29> kDirectoryDomain{
    'R',
    'e',
    's',
    't',
    'o',
    'r',
    'a',
    't',
    'i',
    'o',
    'n',
    'C',
    'o',
    'n',
    't',
    'e',
    'n',
    't',
    'D',
    'i',
    'r',
    'e',
    'c',
    't',
    'o',
    'r',
    'y',
    kDomainTerminator,
    kDirectoryDomainVersion,
};
constexpr std::wstring_view kDirectoryWildcard = L"*";

[[nodiscard]] std::uint64_t file_time_value(const FILETIME& value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
}

[[nodiscard]] std::uint64_t file_size_value(const WIN32_FIND_DATAW& entry) noexcept {
    return (static_cast<std::uint64_t>(entry.nFileSizeHigh) << 32U) | entry.nFileSizeLow;
}

[[nodiscard]] bool make_search_path(std::wstring_view directory,
                                    core::path::Buffer& output) noexcept {
    if (!core::path::assign(output, directory) || output.length == 0) {
        return false;
    }
    const wchar_t tail = output.chars[output.length - 1];
    if (tail != L'\\' && tail != L'/' && !core::path::append(output, L"\\")) {
        return false;
    }
    return core::path::append(output, kDirectoryWildcard);
}

[[nodiscard]] bool name_less(const Candidate& first, const Candidate& second) noexcept {
    return std::string_view(first.parsed.stem.data(), first.parsed.stemLength)
           < std::string_view(second.parsed.stem.data(), second.parsed.stemLength);
}

[[nodiscard]] bool hash_inventory(std::span<const Candidate> candidates,
                                  Fingerprint& output) noexcept {
    output = {};
    fingerprint::Hasher hasher;
    if (!hasher.update(std::as_bytes(std::span(kDirectoryDomain)))
        || !fingerprint::update_u32(hasher, static_cast<std::uint32_t>(candidates.size()))) {
        return false;
    }
    for (const Candidate& candidate : candidates) {
        const package::ParsedName& name = candidate.parsed;
        const auto bytes =
            std::as_bytes(std::span(name.stem.data(), static_cast<std::size_t>(name.stemLength)));
        if (!fingerprint::update_u16(hasher, name.stemLength) || !hasher.update(bytes)
            || !fingerprint::update_u16(hasher, name.packageId)
            || !fingerprint::update_u32(hasher, name.patchIndex)
            || !fingerprint::update_u64(hasher, candidate.fileSize)
            || !fingerprint::update_u64(hasher, candidate.lastWriteTime)) {
            return false;
        }
    }
    return hasher.finish(output);
}

} // namespace

bool inventory(std::wstring_view directory,
               std::span<Candidate> candidates,
               std::size_t& count,
               Fingerprint& fingerprintValue) noexcept {
    count = 0;
    fingerprintValue = {};
    core::path::Buffer searchPath;
    if (!make_search_path(directory, searchPath)) {
        return false;
    }

    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW(searchPath.chars.data(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool complete = true;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        package::ParsedName parsed{};
        const package::NameStatus status = package::parse_file_name(entry.cFileName, parsed);
        if (status == package::NameStatus::unrecognized
            || status == package::NameStatus::excluded) {
            continue;
        }
        if (status == package::NameStatus::invalid || count == candidates.size()) {
            complete = false;
            break;
        }
        Candidate& candidate = candidates[count++];
        candidate = {};
        candidate.parsed = parsed;
        candidate.fileSize = file_size_value(entry);
        candidate.lastWriteTime = file_time_value(entry.ftLastWriteTime);
    } while (FindNextFileW(search, &entry) != FALSE);
    const DWORD enumerationError = GetLastError();
    FindClose(search);
    if (!complete || enumerationError != ERROR_NO_MORE_FILES || count == 0) {
        count = 0;
        return false;
    }

    std::span<Candidate> occupied = candidates.first(count);
    std::sort(occupied.begin(), occupied.end(), name_less);
    for (std::size_t index = 1; index < occupied.size(); ++index) {
        if (!name_less(occupied[index - 1], occupied[index])) {
            count = 0;
            return false;
        }
    }
    return hash_inventory(occupied, fingerprintValue);
}

} // namespace dawn::state::content_manifest::scanner
