#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "layout.h"

namespace dawn::middleware::content::packages::reader {

struct BlockKeys {
    std::array<std::byte, 16> primary{};
    std::array<std::byte, 16> alternate{};
    std::array<std::byte, 12> nonceBase{};
};

inline constexpr std::size_t kPathCapacity = 320;

struct Path {
    std::array<wchar_t, kPathCapacity> chars{};
};

inline constexpr std::size_t kBlockSlack = 64;

// a tag read decodes a whole block for an entry of a few hundred bytes and a walk
// revisits the same block many times, so keeping the last few makes a walk affordable
inline constexpr std::size_t kBlockCacheSlots = 8;
inline constexpr std::size_t kHeaderCacheSlots = 8;

struct BlockSlot {
    std::array<std::byte, layout::kBlockSize + kBlockSlack> bytes{};
    std::size_t size{};
    std::uint64_t key{};
    std::uint64_t used{};
    bool valid{};
};

struct HeaderSlot {
    std::uint64_t key{};
    std::uint64_t entryTable{};
    std::uint64_t blockTable{};
    std::uint32_t entryCount{};
    std::uint32_t blockCount{};
    bool valid{};
};

inline constexpr std::size_t kFileSlots = 4;
inline constexpr std::size_t kTableSlots = 2;
inline constexpr std::size_t kEntryCapacity = 1U << layout::kTagEntryBits;
inline constexpr std::size_t kBlockCapacity = 8192;

struct FileSlot {
    Path path{};
    void* file{};
    std::uint64_t hash{};
    std::uint64_t used{};
};

struct TableSlot {
    Path path{};
    std::array<layout::EntryRecord, kEntryCapacity> entries{};
    std::array<layout::BlockRecord, kBlockCapacity> blocks{};
    std::uint32_t entryCount{};
    std::uint32_t blockCount{};
    std::uint64_t used{};
    bool occupied{};
};

// no two readers share a scratch, so a parallel pass gives each thread its own and nothing
// locks; it is several megabytes, so keep it in static or member storage
struct Scratch {
    std::array<std::byte, layout::kBlockSize> ciphertext{};
    std::array<std::byte, layout::kBlockSize> plaintext{};
    std::array<std::byte, layout::kBlockSize + kBlockSlack> decompressed{};
    std::array<BlockSlot, kBlockCacheSlots> blocks{};
    std::array<HeaderSlot, kHeaderCacheSlots> headers{};
    std::array<FileSlot, kFileSlots> files{};
    std::array<TableSlot, kTableSlots> tables{};
    std::uint64_t useCounter{};
    std::uint64_t slotCounter{};
};

struct Source {
    std::wstring_view directory;
    const BlockKeys* keys{};
};

struct ScanResult {
    std::size_t packages{};
    std::size_t entries{};
    std::size_t matches{};
};

using ClassVisitor = bool (*)(void* context, std::uint32_t tag) noexcept;

struct ClassEntry {
    std::uint32_t tag{};
    std::wstring_view packageFamily;
};

using ClassEntryVisitor = bool (*)(void* context, const ClassEntry& entry) noexcept;

// entry tables are plain file data, so this needs no block keys and runs before any
// are known
[[nodiscard]] bool scan_class(std::wstring_view directory,
                              std::uint32_t classId,
                              ClassVisitor visitor,
                              void* context,
                              ScanResult& result) noexcept;

[[nodiscard]] bool scan_class_entries(std::wstring_view directory,
                                      std::uint32_t classId,
                                      ClassEntryVisitor visitor,
                                      void* context,
                                      ScanResult& result) noexcept;

void release_caches() noexcept;

void close_files(Scratch& scratch) noexcept;

[[nodiscard]] bool read_tag(const Source& source,
                            Scratch& scratch,
                            std::uint32_t tag,
                            std::vector<std::byte>& output) noexcept;

// a chain that hops between blob classes needs the class to pick the parser, and the entry
// record already has it, so no second read
[[nodiscard]] bool read_tag(const Source& source,
                            Scratch& scratch,
                            std::uint32_t tag,
                            std::vector<std::byte>& output,
                            std::uint32_t& classId) noexcept;

} // namespace dawn::middleware::content::packages::reader
