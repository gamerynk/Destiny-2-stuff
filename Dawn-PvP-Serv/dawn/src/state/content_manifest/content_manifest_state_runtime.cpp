#include "content_manifest_state_runtime.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "cache/internal.h"
#include "scanner/internal.h"

namespace dawn::state::content_manifest {
namespace {

constexpr std::wstring_view kCacheDirectorySuffix = L"\\cache";
constexpr std::wstring_view kCacheFileSuffix = L"\\cache\\content_manifest.bin";

struct Storage final {
    SRWLOCK lock{SRWLOCK_INIT};
    std::array<Row, kRowCapacity> rows{};
    std::size_t rowCount{};
    Fingerprint buildFingerprint{};
    Guid guid{};
    bool ready{};
};

Storage g_storage;
std::array<scanner::Candidate, kRowCapacity> g_candidates{};
std::array<Row, kRowCapacity> g_stagedRows{};
std::array<Row, kRowCapacity> g_validationRows{};

void clear_storage() noexcept {
    SecureZeroMemory(g_storage.rows.data(), sizeof g_storage.rows);
    g_storage.rowCount = 0;
    g_storage.buildFingerprint = {};
    g_storage.guid = {};
    g_storage.ready = false;
}

void clear_scratch() noexcept {
    SecureZeroMemory(g_candidates.data(), sizeof g_candidates);
    SecureZeroMemory(g_stagedRows.data(), sizeof g_stagedRows);
    SecureZeroMemory(g_validationRows.data(), sizeof g_validationRows);
}

[[nodiscard]] bool
cache_paths(void* module, core::path::Buffer& directory, core::path::Buffer& path) noexcept {
    core::path::Buffer artifact;
    if (!core::path::artifact_directory(module, artifact)) {
        return false;
    }
    directory = artifact;
    path = artifact;
    return core::path::append(directory, kCacheDirectorySuffix)
           && core::path::append(path, kCacheFileSuffix);
}

void publish(std::span<const Row> rows,
             const Fingerprint& buildFingerprint,
             const Guid& guid) noexcept {
    clear_storage();
    std::copy(rows.begin(), rows.end(), g_storage.rows.begin());
    g_storage.rowCount = rows.size();
    g_storage.buildFingerprint = buildFingerprint;
    g_storage.guid = guid;
    g_storage.ready = true;
}

} // namespace

bool initialize(void* module,
                std::wstring_view packagesDirectory,
                bool allowOfflineSnapshot) noexcept {
    AcquireSRWLockExclusive(&g_storage.lock);
    clear_storage();
    clear_scratch();

    std::size_t candidateCount = 0;
    Fingerprint directoryFingerprint{};
    bool complete =
        scanner::inventory(packagesDirectory, g_candidates, candidateCount, directoryFingerprint);
    const bool inventoryComplete = complete;
    core::path::Buffer cacheDirectory;
    core::path::Buffer cachePath;
    if ((complete || allowOfflineSnapshot) && module != nullptr) {
        complete = cache_paths(module, cacheDirectory, cachePath);
    }

    std::size_t rowCount = 0;
    Fingerprint buildFingerprint{};
    Guid guid{};
    cache::LoadStatus status = cache::LoadStatus::missing;
    if (complete && module != nullptr) {
        status = cache::load(cachePath.chars.data(),
                             directoryFingerprint,
                             g_stagedRows,
                             rowCount,
                             buildFingerprint,
                             guid,
                             !inventoryComplete && allowOfflineSnapshot);
        if (!inventoryComplete) {
            complete = status == cache::LoadStatus::loaded;
        }
    }
    if (complete && status != cache::LoadStatus::loaded) {
        complete = scanner::extract(packagesDirectory,
                                    std::span(g_candidates).first(candidateCount),
                                    directoryFingerprint,
                                    g_stagedRows,
                                    rowCount,
                                    buildFingerprint,
                                    guid);
        if (complete && module != nullptr) {
            complete = cache::write(cacheDirectory.chars.data(),
                                    cachePath.chars.data(),
                                    directoryFingerprint,
                                    buildFingerprint,
                                    std::span(g_stagedRows).first(rowCount),
                                    g_validationRows);
        }
    }
    if (complete) {
        publish(std::span(g_stagedRows).first(rowCount), buildFingerprint, guid);
    }
    clear_scratch();
    ReleaseSRWLockExclusive(&g_storage.lock);
    return complete;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_storage.lock);
    clear_storage();
    clear_scratch();
    ReleaseSRWLockExclusive(&g_storage.lock);
}

bool visit_snapshot(SnapshotVisitor visitor, void* context) noexcept {
    if (visitor == nullptr) {
        return false;
    }
    AcquireSRWLockShared(&g_storage.lock);
    if (!g_storage.ready) {
        ReleaseSRWLockShared(&g_storage.lock);
        return false;
    }
    const View view{
        std::span(g_storage.rows).first(g_storage.rowCount),
        std::string_view(g_storage.guid.data(), kGuidCapacity - 1),
        std::span<const std::byte, kFingerprintSize>(g_storage.buildFingerprint),
    };
    // the view must not escape: the read lock is released right after this callback
    const bool result = visitor(context, view);
    ReleaseSRWLockShared(&g_storage.lock);
    return result;
}

} // namespace dawn::state::content_manifest
