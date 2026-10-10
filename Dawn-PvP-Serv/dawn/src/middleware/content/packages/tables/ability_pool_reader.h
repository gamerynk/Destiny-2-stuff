#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::content::packages::tables::abilities {

inline constexpr std::size_t kEntryCapacity = 64;
inline constexpr std::size_t kPoolRecordCapacity = 64;
inline constexpr std::uint8_t kEmptyByte = 0xFF;
// no-hash sentinel: entry has no plug source of its own
inline constexpr std::uint32_t kNoPlugSource = 0x811C9DC5U;

struct Entry {
    std::uint32_t plugSource{kNoPlugSource};
    // entries in one group are alternatives, only one is active
    std::uint8_t group{kEmptyByte};
    std::uint8_t kind{kEmptyByte};
    std::uint32_t poolTag{};
    bool hasSelector{};
    std::uint8_t selector{kEmptyByte};
};

struct PoolRecord {
    std::uint32_t definitionHash{kNoPlugSource};
    std::uint8_t category{kEmptyByte};
    std::uint8_t linkEntry{kEmptyByte};
    std::uint8_t linkSubgroup{kEmptyByte};
    std::uint8_t linkElement{kEmptyByte};
    std::uint8_t kind{kEmptyByte};
    std::uint8_t destination{kEmptyByte};
};

[[nodiscard]] std::size_t read_entries(std::span<const std::byte> blob,
                                       std::span<Entry> output) noexcept;

// active variant depends on group count and the entry's selector, so the entry is needed
[[nodiscard]] std::size_t read_pool_records(std::span<const std::byte> blob,
                                            const Entry& entry,
                                            std::uint8_t subgroup,
                                            std::span<PoolRecord> output) noexcept;

} // namespace dawn::middleware::content::packages::tables::abilities
