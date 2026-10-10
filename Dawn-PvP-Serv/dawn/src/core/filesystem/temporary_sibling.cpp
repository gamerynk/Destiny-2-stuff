#include "temporary_sibling.h"

#include <Windows.h>

#include <cstddef>
#include <string_view>

#include "path.h"

namespace dawn::core::path {
namespace {

constexpr std::wstring_view kTemporarySuffix = L".tmp";
constexpr std::wstring_view kComponentSeparator = L".";
constexpr std::wstring_view kSearchSuffix = L".*.tmp";
constexpr std::wstring_view kDirectorySeparators = L"\\/";
constexpr std::size_t kPathSeparatorWidth = 1;
constexpr std::size_t kHexadecimalDigitsPerByte = 2;
constexpr std::size_t kWriterIdentifierDigits = sizeof(DWORD) * kHexadecimalDigitsPerByte;
constexpr std::size_t kWriterComponentCount = 3;
constexpr std::size_t kProcessComponentIndex = 0;
constexpr std::size_t kBitsPerHexadecimalDigit = 4;
constexpr DWORD kAlphabeticHexadecimalOffset = 10;
constexpr std::size_t kMaximumEntriesPerSweep = 64;
constexpr std::size_t kMaximumRemovalsPerSweep = 8;
// a zero-duration wait observes process state without blocking a write
constexpr DWORD kNonBlockingWaitMilliseconds = 0;
constexpr ULONGLONG kFileTimeTicksPerSecond = 10'000'000ULL;
// one hour keeps recent ownership transitions out of crash-residue cleanup
constexpr ULONGLONG kMinimumStaleAgeSeconds = 60ULL * 60ULL;
constexpr ULONGLONG kMinimumStaleAgeTicks = kMinimumStaleAgeSeconds * kFileTimeTicksPerSecond;

[[nodiscard]] bool hexadecimal_value(wchar_t character, DWORD& value) noexcept {
    if (character >= L'0' && character <= L'9') {
        value = static_cast<DWORD>(character - L'0');
        return true;
    }
    if (character >= L'A' && character <= L'F') {
        value = static_cast<DWORD>(character - L'A') + kAlphabeticHexadecimalOffset;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_identifier(std::wstring_view text, DWORD& value) noexcept {
    if (text.size() != kWriterIdentifierDigits) {
        return false;
    }
    value = 0;
    for (const wchar_t character : text) {
        DWORD digit = 0;
        if (!hexadecimal_value(character, digit)) {
            return false;
        }
        value = (value << kBitsPerHexadecimalDigit) | digit;
    }
    return true;
}

[[nodiscard]] bool
parse_owner(std::wstring_view finalName, std::wstring_view candidate, DWORD& processId) noexcept {
    const std::size_t writerSuffixLength =
        kWriterComponentCount * (kComponentSeparator.size() + kWriterIdentifierDigits)
        + kTemporarySuffix.size();
    if (candidate.size() != finalName.size() + writerSuffixLength
        || candidate.substr(0, finalName.size()) != finalName) {
        return false;
    }

    std::size_t offset = finalName.size();
    for (std::size_t component = 0; component < kWriterComponentCount; ++component) {
        if (candidate.substr(offset, kComponentSeparator.size()) != kComponentSeparator) {
            return false;
        }
        offset += kComponentSeparator.size();
        DWORD identifier = 0;
        if (!parse_identifier(candidate.substr(offset, kWriterIdentifierDigits), identifier)) {
            return false;
        }
        if (component == kProcessComponentIndex) {
            processId = identifier;
        }
        offset += kWriterIdentifierDigits;
    }
    return candidate.substr(offset) == kTemporarySuffix;
}

[[nodiscard]] ULONGLONG file_time_ticks(const FILETIME& value) noexcept {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}

[[nodiscard]] bool old_enough(const FILETIME& lastWrite, const FILETIME& now) noexcept {
    const ULONGLONG writtenTicks = file_time_ticks(lastWrite);
    const ULONGLONG currentTicks = file_time_ticks(now);
    return currentTicks >= writtenTicks && currentTicks - writtenTicks >= kMinimumStaleAgeTicks;
}

// confirm the owner stopped without guessing through access failures
[[nodiscard]] bool owner_stopped(DWORD processId) noexcept {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (process == nullptr) {
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }
    const DWORD waitResult = WaitForSingleObject(process, kNonBlockingWaitMilliseconds);
    (void)CloseHandle(process);
    return waitResult == WAIT_OBJECT_0;
}

[[nodiscard]] bool make_candidate_path(std::wstring_view finalPath,
                                       std::size_t leafOffset,
                                       std::wstring_view candidateName,
                                       Buffer& candidatePath) noexcept {
    return assign(candidatePath, finalPath.substr(0, leafOffset))
           && append(candidatePath, candidateName);
}

} // namespace

void remove_stale_siblings(const wchar_t* finalPath) noexcept {
    if (finalPath == nullptr) {
        return;
    }
    const std::wstring_view completePath(finalPath);
    if (completePath.empty()) {
        return;
    }
    const std::size_t separator = completePath.find_last_of(kDirectorySeparators);
    const std::size_t leafOffset =
        separator == std::wstring_view::npos ? 0 : separator + kPathSeparatorWidth;
    const std::wstring_view finalName = completePath.substr(leafOffset);

    Buffer searchPath;
    if (finalName.empty() || !assign(searchPath, completePath)
        || !append(searchPath, kSearchSuffix)) {
        return;
    }

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW(searchPath.chars.data(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }

    std::size_t inspected = 0;
    std::size_t removed = 0;
    do {
        if (inspected >= kMaximumEntriesPerSweep || removed >= kMaximumRemovalsPerSweep) {
            break;
        }
        ++inspected;
        const std::wstring_view candidateName(entry.cFileName);
        DWORD ownerProcessId = 0;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            || !parse_owner(finalName, candidateName, ownerProcessId)
            || !old_enough(entry.ftLastWriteTime, now) || !owner_stopped(ownerProcessId)) {
            continue;
        }

        Buffer candidatePath;
        if (!make_candidate_path(completePath, leafOffset, candidateName, candidatePath)) {
            continue;
        }
        // writer handles deny delete sharing, so an active write stays protected here
        if (DeleteFileW(candidatePath.chars.data()) != FALSE) {
            ++removed;
        }
    } while (FindNextFileW(search, &entry) != FALSE);
    (void)FindClose(search);
}

} // namespace dawn::core::path
