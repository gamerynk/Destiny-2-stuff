#include <array>
#include <cstddef>

#include "locator_cache.h"

namespace dawn::middleware::content::packages::reader::locator_cache {
namespace {

// 528 installed packages measured; at 16 entries a package change missed and re-read all
// 2,202 installed file names
constexpr std::size_t kCapacity = 1024;
constexpr std::uint64_t kHashBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

struct Entry {
    std::uint64_t directoryHash{};
    std::uint32_t patchIndex{};
    std::uint16_t packageId{};
    bool occupied{};
    Path stem{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Entry, kCapacity> g_entries{};
std::size_t g_cursor = 0;
std::uint64_t g_completeDirectory = 0;

} // namespace

std::uint64_t directory_hash(std::wstring_view value) noexcept {
    std::uint64_t hash = kHashBasis;
    for (const wchar_t character : value) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= kHashPrime;
    }
    return hash;
}

bool find(std::uint64_t directoryHash,
          std::uint16_t packageId,
          Path& stem,
          std::uint32_t& patchIndex) noexcept {
    bool found = false;
    AcquireSRWLockShared(&g_lock);
    for (const Entry& entry : g_entries) {
        if (entry.occupied && entry.packageId == packageId
            && entry.directoryHash == directoryHash) {
            stem = entry.stem;
            patchIndex = entry.patchIndex;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

void store(std::uint64_t directoryHash,
           std::uint16_t packageId,
           const Path& stem,
           std::uint32_t patchIndex) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    std::size_t slot = g_entries.size();
    for (std::size_t index = 0; index < g_entries.size() && slot == g_entries.size(); ++index) {
        if (!g_entries[index].occupied) {
            slot = index;
            break;
        }
        if (g_entries[index].packageId == packageId
            && g_entries[index].directoryHash == directoryHash) {
            if (g_entries[index].patchIndex >= patchIndex) {
                ReleaseSRWLockExclusive(&g_lock);
                return;
            }
            slot = index;
        }
    }
    if (slot == g_entries.size()) {
        slot = g_cursor;
        g_cursor = (g_cursor + 1U) % g_entries.size();
    }
    g_entries[slot] = Entry{directoryHash, patchIndex, packageId, true, stem};
    ReleaseSRWLockExclusive(&g_lock);
}

bool complete(std::uint64_t directoryHash) noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool read = directoryHash != 0 && g_completeDirectory == directoryHash;
    ReleaseSRWLockShared(&g_lock);
    return read;
}

void mark_complete(std::uint64_t directoryHash) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_completeDirectory = directoryHash;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::middleware::content::packages::reader::locator_cache
