#include "temporary_cache_file.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../../../core/filesystem/temporary_sibling.h"
#include "../../records/format.h"
#include "../cache_payload_writer.h"

namespace dawn::state::build_data::cache::temporary {
namespace {

constexpr std::wstring_view kComponentSeparator = L".";
constexpr std::wstring_view kSuffix = L".tmp";
constexpr std::size_t kHexadecimalDigitsPerByte = 2;
constexpr std::size_t kWriterIdentifierDigits = sizeof(DWORD) * kHexadecimalDigitsPerByte;
constexpr std::size_t kBitsPerHexadecimalDigit = 4;
constexpr DWORD kHexadecimalDigitMask = (1UL << kBitsPerHexadecimalDigit) - 1UL;
constexpr std::wstring_view kHexadecimalDigits = L"0123456789ABCDEF";
constexpr std::size_t kCreateAttemptCapacity = 16;

volatile LONG g_sequence = 0;

enum class WriteStatus {
    complete,
    collision,
    failed,
};

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

[[nodiscard]] bool make_path(const wchar_t* finalPath, core::path::Buffer& temporaryPath) noexcept {
    const DWORD sequence = static_cast<DWORD>(InterlockedIncrement(&g_sequence));
    return core::path::assign(temporaryPath, finalPath)
           && append_identifier(temporaryPath, GetCurrentProcessId())
           && append_identifier(temporaryPath, GetCurrentThreadId())
           && append_identifier(temporaryPath, sequence)
           && core::path::append(temporaryPath, kSuffix);
}

[[nodiscard]] WriteStatus write_file(const wchar_t* temporaryPath,
                                     const BuildIdentity& build,
                                     records::Domains domains,
                                     std::uint64_t checksum) noexcept {
    const HANDLE file = CreateFileW(temporaryPath,
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? WriteStatus::collision
                                                                           : WriteStatus::failed;
    }
    const records::Header header{
        records::kCacheMagic,
        records::kCacheFormatVersion,
        build.imageTimestamp,
        build.imageSize,
        build.configuredEquipmentHash,
        static_cast<std::uint32_t>(domains.named.size()),
        static_cast<std::uint32_t>(domains.items.size()),
        static_cast<std::uint32_t>(domains.collectibles.size()),
        static_cast<std::uint32_t>(domains.materialRequirementSets.size()),
        static_cast<std::uint32_t>(domains.itemDetails.size()),
        static_cast<std::uint32_t>(domains.socketPlugRules.size()),
        static_cast<std::uint32_t>(domains.socketPlugPools.size()),
        static_cast<std::uint32_t>(domains.socketPlugMembers.size()),
        static_cast<std::uint32_t>(domains.inventoryBuckets.size()),
        static_cast<std::uint32_t>(domains.socketEntryLists.size()),
        static_cast<std::uint32_t>(domains.socketEntryTables.size()),
        static_cast<std::uint32_t>(domains.abilityBuckets.size()),
        static_cast<std::uint32_t>(domains.progressions.size()),
        static_cast<std::uint32_t>(domains.scenarios.size()),
        static_cast<std::uint32_t>(domains.rosterGroups.size()),
        static_cast<std::uint32_t>(domains.spawnStems.size()),
        static_cast<std::uint32_t>(domains.spawnNameHashes.size()),
        static_cast<std::uint32_t>(domains.spawnPoints.size()),
        static_cast<std::uint32_t>(domains.hashNames.size()),
        static_cast<std::uint32_t>(domains.vendorIndex.size()),
        static_cast<std::uint32_t>(domains.vendorDefinitions.size()),
        static_cast<std::uint32_t>(domains.vendorSaleRows.size()),
        static_cast<std::uint32_t>(domains.vendorInstalledRows.size()),
        domains.constants,
        checksum,
    };
    DWORD transferred = 0;
    bool complete = WriteFile(file, &header, sizeof header, &transferred, nullptr) != FALSE
                    && transferred == sizeof header && writer::write_payload(file, domains)
                    && FlushFileBuffers(file) != FALSE;
    complete = CloseHandle(file) != FALSE && complete;
    if (!complete) {
        // this writer owns a CREATE_NEW path, so a failed partial file is safe to remove
        (void)DeleteFileW(temporaryPath);
    }
    return complete ? WriteStatus::complete : WriteStatus::failed;
}

} // namespace

bool write(const wchar_t* finalPath,
           const BuildIdentity& build,
           records::Domains domains,
           std::uint64_t checksum,
           core::path::Buffer& temporaryPath) noexcept {
    core::path::remove_stale_siblings(finalPath);
    for (std::size_t attempt = 0; attempt < kCreateAttemptCapacity; ++attempt) {
        if (!make_path(finalPath, temporaryPath)) {
            return false;
        }
        const WriteStatus status = write_file(temporaryPath.chars.data(), build, domains, checksum);
        if (status == WriteStatus::complete) {
            return true;
        }
        if (status == WriteStatus::failed) {
            return false;
        }
    }
    return false;
}

} // namespace dawn::state::build_data::cache::temporary
