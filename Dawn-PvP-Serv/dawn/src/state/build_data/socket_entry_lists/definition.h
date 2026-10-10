#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::socket_entry_lists {

inline constexpr std::size_t kDefinitionCapacity = 32768;
inline constexpr std::size_t kEntryCapacity = 36;
inline constexpr std::uint8_t kAbsentEntryState = 0;
inline constexpr std::uint8_t kReadyEntryState = 16;

inline constexpr std::uint8_t kActiveEntryState = 18;
// the super lane is active although it carries no plug source
inline constexpr std::uint8_t kSuperEntryKind = 34;
inline constexpr std::uint8_t kNoEntryGroup = 0xFF;
inline constexpr std::uint32_t kNoPlugSource = 0x811C9DC5U;

struct Entry {
    std::uint32_t plugSource{};
    std::uint8_t group{kNoEntryGroup};
    std::uint8_t kind{};
};

struct Definition {
    std::uint32_t definitionHash{};
    std::uint16_t definitionIndex{};
    std::uint8_t entryCount{};
    std::uint64_t readyMask{};
};

inline constexpr std::uint16_t kNoEntryTable = 0xFFFF;

// only lists with a super lane (the 9 subclasses) keep this; on every definition it would cost 10 MB
struct EntryTable {
    std::uint16_t definitionIndex{kNoEntryTable};
    std::array<Entry, kEntryCapacity> entries{};
};

inline constexpr std::size_t kEntryTableCapacity = 32;

} // namespace dawn::state::build_data::socket_entry_lists
