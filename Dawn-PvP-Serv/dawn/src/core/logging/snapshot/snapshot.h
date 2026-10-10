#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../log.h"

namespace dawn::core::log::snapshot {

inline constexpr std::size_t kEntryCapacity = 128;

namespace internal {

void record(Channel channel, Level level, std::string_view text) noexcept;

} // namespace internal

class Entry final {
public:
    [[nodiscard]] Channel channel() const noexcept;

    [[nodiscard]] Level level() const noexcept;

    [[nodiscard]] std::string_view text() const noexcept;

private:
    friend void internal::record(Channel channel, Level level, std::string_view text) noexcept;

    Channel channel_{Channel::core};
    Level level_{Level::error};
    std::array<char, kLineCapacity> text_{};
    std::size_t textLength_{};
};

class Snapshot final {
public:
    [[nodiscard]] std::span<const Entry> entries() const noexcept;

    [[nodiscard]] std::uint64_t overwritten_count() const noexcept;

private:
    friend Snapshot take() noexcept;

    std::array<Entry, kEntryCapacity> entries_{};
    std::size_t count_{};
    std::uint64_t overwrittenCount_{};
};

[[nodiscard]] Snapshot take() noexcept;

} // namespace dawn::core::log::snapshot
