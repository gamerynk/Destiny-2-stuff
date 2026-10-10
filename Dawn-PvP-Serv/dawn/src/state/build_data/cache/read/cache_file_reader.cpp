#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../../core/filesystem/path.h"
#include "../internal.h"
#include "cache_payload_reader.h"

namespace dawn::state::build_data::cache {
namespace {

[[nodiscard]] bool required_domains_present(const records::DomainCounts& counts) noexcept {
    return counts.named != 0 && counts.items != 0 && counts.collectibles != 0
           && counts.materialRequirementSets != 0 && counts.socketPlugRules != 0
           && counts.socketPlugPools != 0 && counts.inventoryBuckets != 0
           && counts.socketEntryLists != 0 && counts.progressions != 0 && counts.scenarios != 0;
}

[[nodiscard]] bool counts_fit(const records::DomainCounts& counts,
                              records::MutableDomains output) noexcept {
    return counts.named <= output.named.size() && counts.items <= output.items.size()
           && counts.collectibles <= output.collectibles.size()
           && counts.materialRequirementSets <= output.materialRequirementSets.size()
           && counts.itemDetails <= output.itemDetails.size()
           && counts.socketPlugRules <= output.socketPlugRules.size()
           && counts.socketPlugPools <= output.socketPlugPools.size()
           && counts.socketPlugMembers <= output.socketPlugMembers.size()
           && counts.inventoryBuckets <= output.inventoryBuckets.size()
           && counts.socketEntryLists <= output.socketEntryLists.size()
           && counts.socketEntryTables <= output.socketEntryTables.size()
           && counts.abilityBuckets <= output.abilityBuckets.size()
           && counts.progressions <= output.progressions.size()
           && counts.scenarios <= output.scenarios.size()
           && counts.rosterGroups <= output.rosterGroups.size()
           && counts.spawnStems <= output.spawnStems.size()
           && counts.spawnNameHashes <= output.spawnNameHashes.size()
           && counts.spawnPoints <= output.spawnPoints.size()
           && counts.hashNames <= output.hashNames.size()
           && counts.vendorIndex <= output.vendorIndex.size()
           && counts.vendorDefinitions <= output.vendorDefinitions.size()
           && counts.vendorSaleRows <= output.vendorSaleRows.size()
           && counts.vendorInstalledRows <= output.vendorInstalledRows.size();
}

[[nodiscard]] records::DomainCounts counts_of(const records::Header& header) noexcept {
    return {
        header.namedCount,
        header.itemCount,
        header.collectibleCount,
        header.materialRequirementSetCount,
        header.itemDetailCount,
        header.socketPlugRuleCount,
        header.socketPlugPoolCount,
        header.socketPlugMemberCount,
        header.inventoryBucketCount,
        header.socketEntryListCount,
        header.socketEntryTableCount,
        header.abilityBucketCount,
        header.progressionCount,
        header.scenarioCount,
        header.rosterGroupCount,
        header.spawnStemCount,
        header.spawnNameHashCount,
        header.spawnPointCount,
        header.hashNameCount,
        header.vendorIndexCount,
        header.vendorDefinitionCount,
        header.vendorSaleRowCount,
        header.vendorInstalledRowCount,
    };
}

[[nodiscard]] bool stale_format(std::uint32_t version) noexcept {
    return version < records::kCacheFormatVersion;
}

[[nodiscard]] LoadStatus close_with(HANDLE file, LoadStatus status) noexcept {
    return CloseHandle(file) != FALSE ? status : LoadStatus::invalid;
}

// the cache describes the installed game image, not the process hosting State
constexpr std::wstring_view kInstalledGameExecutable = L"destiny2.exe";

[[nodiscard]] bool read_exact(HANDLE file, void* output, DWORD size) noexcept {
    DWORD received{};
    return ReadFile(file, output, size, &received, nullptr) != FALSE && received == size;
}

// client and dedicated server both deploy beside the game exe, so they share one cache identity
[[nodiscard]] bool installed_image_identity(std::uint64_t configuredEquipmentHash,
                                            BuildIdentity& identity) noexcept {
    core::path::Buffer imagePath;
    if (!core::path::module_directory(GetModuleHandleW(nullptr), imagePath)
        || !core::path::append(imagePath, kInstalledGameExecutable)) {
        return false;
    }
    const HANDLE file = CreateFileW(imagePath.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    bool valid = read_exact(file, &dos, sizeof dos) && dos.e_magic == IMAGE_DOS_SIGNATURE
                 && dos.e_lfanew > 0;
    if (valid) {
        LARGE_INTEGER offset{};
        offset.QuadPart = dos.e_lfanew;
        valid = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE
                && read_exact(file, &nt, sizeof nt) && nt.Signature == IMAGE_NT_SIGNATURE
                && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
                && nt.OptionalHeader.SizeOfImage != 0;
    }
    valid = CloseHandle(file) != FALSE && valid;
    if (!valid) {
        return false;
    }
    identity.imageTimestamp = nt.FileHeader.TimeDateStamp;
    identity.imageSize = nt.OptionalHeader.SizeOfImage;
    identity.configuredEquipmentHash = configuredEquipmentHash;
    return true;
}

[[nodiscard]] bool loaded_image_identity(std::uint64_t configuredEquipmentHash,
                                         BuildIdentity& identity) noexcept {
    const HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return false;
    }
    const auto* image = reinterpret_cast<const std::byte*>(module);
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, image, sizeof dos);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return false;
    }
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, image + dos.e_lfanew, sizeof nt);
    if (nt.Signature != IMAGE_NT_SIGNATURE
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.OptionalHeader.SizeOfImage == 0) {
        return false;
    }
    identity.imageTimestamp = nt.FileHeader.TimeDateStamp;
    identity.imageSize = nt.OptionalHeader.SizeOfImage;
    identity.configuredEquipmentHash = configuredEquipmentHash;
    return true;
}

} // namespace

bool current_build_identity(std::uint64_t configuredEquipmentHash,
                            BuildIdentity& identity) noexcept {
    identity = {};
    return installed_image_identity(configuredEquipmentHash, identity)
           || loaded_image_identity(configuredEquipmentHash, identity);
}

LoadStatus load(const wchar_t* path,
                const BuildIdentity& expectedBuild,
                records::MutableDomains output,
                records::DomainCounts& counts) noexcept {
    counts = {};
    read::clear(output);
    if (path == nullptr || expectedBuild.imageSize == 0 || output.constants == nullptr) {
        return LoadStatus::invalid;
    }
    const HANDLE file = CreateFileW(path,
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? LoadStatus::missing
                                                                              : LoadStatus::invalid;
    }

    LARGE_INTEGER actualSize{};
    records::Prefix prefix{};
    if (GetFileSizeEx(file, &actualSize) == FALSE || actualSize.QuadPart <= 0
        || !read::read_value(file, prefix) || prefix.magic != records::kCacheMagic) {
        return close_with(file, LoadStatus::invalid);
    }
    if (stale_format(prefix.version)) {
        return close_with(file, LoadStatus::stale);
    }
    if (prefix.version != records::kCacheFormatVersion) {
        return close_with(file, LoadStatus::invalid);
    }

    LARGE_INTEGER beginning{};
    records::Header header{};
    if (SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) == FALSE
        || !read::read_value(file, header) || header.magic != records::kCacheMagic
        || header.version != records::kCacheFormatVersion) {
        return close_with(file, LoadStatus::invalid);
    }
    const BuildIdentity cachedBuild{
        header.imageTimestamp,
        header.imageSize,
        header.configuredEquipmentHash,
    };
    if (!(cachedBuild == expectedBuild)) {
        return close_with(file, LoadStatus::stale);
    }

    const records::DomainCounts pendingCounts = counts_of(header);
    std::uint64_t expectedSize = 0;
    std::uint64_t checksum = 0;
    bool valid = required_domains_present(pendingCounts) && counts_fit(pendingCounts, output)
                 && read::expected_size(pendingCounts, expectedSize)
                 && static_cast<std::uint64_t>(actualSize.QuadPart) == expectedSize
                 && read::read_payload(file, header.constants, pendingCounts, output, checksum)
                 && checksum == header.payloadChecksum;
    const LoadStatus status = close_with(file, valid ? LoadStatus::loaded : LoadStatus::invalid);
    if (status != LoadStatus::loaded) {
        read::clear(output);
        return status;
    }
    counts = pendingCounts;
    *output.constants = header.constants;
    return LoadStatus::loaded;
}

} // namespace dawn::state::build_data::cache
