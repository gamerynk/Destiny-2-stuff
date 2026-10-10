#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "../snapshot/snapshot.h"

namespace dawn::core::log::view {

inline constexpr std::size_t kTextFilterCapacity = 128;

struct Filter {
    std::optional<Channel> channel;
    std::optional<Level> level;
    std::array<char, kTextFilterCapacity> text{};
    std::size_t textLength{};
};

class Result final {
public:
    [[nodiscard]] std::span<const snapshot::Entry* const> entries() const noexcept;

    [[nodiscard]] std::size_t source_count() const noexcept;

    [[nodiscard]] std::uint64_t overwritten_count() const noexcept;

private:
    friend Result select(const snapshot::Snapshot& source, const Filter& filter) noexcept;

    std::array<const snapshot::Entry*, snapshot::kEntryCapacity> entries_{};
    std::size_t count_{};
    std::size_t sourceCount_{};
    std::uint64_t overwrittenCount_{};
};

[[nodiscard]] bool set_text(Filter& filter, std::string_view text) noexcept;

[[nodiscard]] Result select(const snapshot::Snapshot& source, const Filter& filter) noexcept;

} // namespace dawn::core::log::view
