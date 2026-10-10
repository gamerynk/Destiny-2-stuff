#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::client::patterns {

struct PatternByte {
    std::byte value{};
    bool exact{};
};

struct Pattern {
    std::string_view name;
    std::span<const PatternByte> bytes;
};

enum class MatchStatus : unsigned char {
    invalid,
    missing,
    unique,
    ambiguous,
};

struct Match {
    MatchStatus status{MatchStatus::invalid};
    std::byte* address{};
};

struct ImageRange {
    std::span<std::byte> bytes;
};

[[nodiscard]] bool resolve_all(std::span<std::byte> image,
                               std::span<const Pattern> patterns,
                               std::span<Match> matches) noexcept;

[[nodiscard]] bool resolve_all(std::span<const ImageRange> image,
                               std::span<const Pattern> patterns,
                               std::span<Match> matches) noexcept;

[[nodiscard]] std::size_t collect_matches(std::span<const ImageRange> image,
                                          const Pattern& pattern,
                                          std::span<std::byte*> output) noexcept;

} // namespace dawn::client::patterns
