#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../../core/filesystem/path.h"
#include "../../../core/filesystem/temporary_sibling.h"
#include "../content_manifest_row_validation.h"
#include "format.h"
#include "internal.h"

namespace dawn::state::content_manifest::cache {
namespace {

constexpr std::size_t kTemporaryNameAttempts = 8;
constexpr std::wstring_view kComponentSeparator = L".";
constexpr std::wstring_view kTemporarySuffix = L".tmp";
constexpr std::size_t kHexadecimalDigitsPerByte = 2;
constexpr std::size_t kWriterIdentifierDigits = sizeof(DWORD) * kHexadecimalDigitsPerByte;
constexpr std::size_t kBitsPerHexadecimalDigit = 4;
constexpr DWORD kHexadecimalDigitMask = (1UL << kBitsPerHexadecimalDigit) - 1UL;
constexpr std::wstring_view kHexadecimalDigits = L"0123456789ABCDEF";

volatile LONG g_temporaryCounter{};

template <typename Value> [[nodiscard]] bool write_exact(HANDLE file, const Value& value) noexcept {
    DWORD copied = 0;
    return sizeof value <= (std::numeric_limits<DWORD>::max)()
           && WriteFile(file, &value, static_cast<DWORD>(sizeof value), &copied, nullptr) != FALSE
           && copied == sizeof value;
}

void encode_row(const Row& row, DiskRow& output) noexcept {
    output = {};
    output.name = row.name;
    output.nameLength = row.nameLength;
    output.packageId = row.packageId;
    output.buildSignature = row.buildSignature;
}

[[nodiscard]] bool append_identifier(core::path::Buffer& path, DWORD value) noexcept {
    if (!core::path::append(path, kComponentSeparator)
        || path.length + kWriterIdentifierDigits >= path.chars.size()) {
        return false;
    }
    for (std::size_t digit = 0; digit < kWriterIdentifierDigits; ++digit) {
        const std::size_t remainingDigits = kWriterIdentifierDigits - digit - 1U;
        const std::size_t shift = remainingDigits * kBitsPerHexadecimalDigit;
        const std::size_t character = (value >> shift) & kHexadecimalDigitMask;
        path.chars[path.length++] = kHexadecimalDigits[character];
    }
    path.chars[path.length] = L'\0';
    return true;
}

[[nodiscard]] bool
make_temporary_path(const wchar_t* finalPath, LONG counter, core::path::Buffer& output) noexcept {
    return finalPath != nullptr && core::path::assign(output, finalPath)
           && append_identifier(output, GetCurrentProcessId())
           && append_identifier(output, GetCurrentThreadId())
           && append_identifier(output, static_cast<DWORD>(counter))
           && core::path::append(output, kTemporarySuffix);
}

[[nodiscard]] HANDLE open_temporary(const wchar_t* finalPath,
                                    core::path::Buffer& temporaryPath) noexcept {
    for (std::size_t attempt = 0; attempt < kTemporaryNameAttempts; ++attempt) {
        const LONG counter = InterlockedIncrement(&g_temporaryCounter);
        if (!make_temporary_path(finalPath, counter, temporaryPath)) {
            return INVALID_HANDLE_VALUE;
        }
        const HANDLE file = CreateFileW(temporaryPath.chars.data(),
                                        GENERIC_READ | GENERIC_WRITE,
                                        0,
                                        nullptr,
                                        CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                        nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) {
            return file;
        }
    }
    return INVALID_HANDLE_VALUE;
}

[[nodiscard]] bool ensure_directory(const wchar_t* directory) noexcept {
    if (directory == nullptr) {
        return false;
    }
    if (CreateDirectoryW(directory, nullptr) != FALSE) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(directory);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

[[nodiscard]] bool validate_temporary(const wchar_t* path,
                                      const Fingerprint& directoryFingerprint,
                                      const Fingerprint& buildFingerprint,
                                      std::span<const Row> rows,
                                      std::span<Row> validationRows) noexcept {
    std::size_t count = 0;
    Fingerprint loadedFingerprint{};
    Guid loadedGuid{};
    const LoadStatus status =
        load(path, directoryFingerprint, validationRows, count, loadedFingerprint, loadedGuid);
    return status == LoadStatus::loaded && count == rows.size()
           && loadedFingerprint == buildFingerprint
           && std::equal(rows.begin(),
                         rows.end(),
                         validationRows.begin(),
                         [](const Row& left, const Row& right) {
                             return left.name == right.name && left.nameLength == right.nameLength
                                    && left.packageId == right.packageId
                                    && left.buildSignature == right.buildSignature;
                         });
}

} // namespace

bool write(const wchar_t* directory,
           const wchar_t* path,
           const Fingerprint& directoryFingerprint,
           const Fingerprint& buildFingerprint,
           std::span<const Row> rows,
           std::span<Row> validationRows) noexcept {
    if (!valid(rows) || validationRows.size() < kRowCapacity || !ensure_directory(directory)) {
        return false;
    }
    core::path::remove_stale_siblings(path);
    Header header{};
    header.magic = kCacheMagic;
    header.version = kCacheVersion;
    header.headerSize = sizeof(Header);
    header.rowSize = sizeof(DiskRow);
    header.rowCount = static_cast<std::uint32_t>(rows.size());
    header.directoryFingerprint = directoryFingerprint;
    header.buildFingerprint = buildFingerprint;

    core::path::Buffer temporaryPath;
    const HANDLE file = open_temporary(path, temporaryPath);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool complete = write_exact(file, header);
    for (const Row& row : rows) {
        DiskRow diskRow{};
        encode_row(row, diskRow);
        if (!complete || !write_exact(file, diskRow)) {
            complete = false;
            break;
        }
    }
    complete = complete && FlushFileBuffers(file) != FALSE;
    complete = CloseHandle(file) != FALSE && complete;
    complete = complete
               && validate_temporary(temporaryPath.chars.data(),
                                     directoryFingerprint,
                                     buildFingerprint,
                                     rows,
                                     validationRows);
    if (complete) {
        // final name changes only after the temp file reopens as a complete valid cache
        complete = MoveFileExW(temporaryPath.chars.data(),
                               path,
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                   != FALSE;
    }
    if (!complete) {
        DeleteFileW(temporaryPath.chars.data());
    }
    return complete;
}

} // namespace dawn::state::content_manifest::cache
