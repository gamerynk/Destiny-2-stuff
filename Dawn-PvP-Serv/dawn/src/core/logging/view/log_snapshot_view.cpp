#include "log_snapshot_view.h"

#include <algorithm>

namespace dawn::core::log::view {
namespace {

[[nodiscard]] char fold_ascii(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

[[nodiscard]] bool contains_text(std::string_view text, std::string_view query) noexcept {
    if (query.empty()) {
        return true;
    }
    const auto match = std::search(
        text.begin(), text.end(), query.begin(), query.end(), [](char left, char right) noexcept {
            return fold_ascii(left) == fold_ascii(right);
        });
    return match != text.end();
}

[[nodiscard]] bool matches(const snapshot::Entry& entry, const Filter& filter) noexcept {
    if (filter.channel.has_value() && entry.channel() != *filter.channel) {
        return false;
    }
    if (filter.level.has_value() && entry.level() != *filter.level) {
        return false;
    }
    const std::size_t textLength = (std::min)(filter.textLength, filter.text.size());
    return contains_text(entry.text(), std::string_view(filter.text.data(), textLength));
}

} // namespace

std::span<const snapshot::Entry* const> Result::entries() const noexcept {
    return {entries_.data(), count_};
}

std::size_t Result::source_count() const noexcept {
    return sourceCount_;
}

std::uint64_t Result::overwritten_count() const noexcept {
    return overwrittenCount_;
}

bool set_text(Filter& filter, std::string_view text) noexcept {
    if (text.size() > filter.text.size()) {
        return false;
    }
    filter.text = {};
    std::copy(text.begin(), text.end(), filter.text.begin());
    filter.textLength = text.size();
    return true;
}

Result select(const snapshot::Snapshot& source, const Filter& filter) noexcept {
    Result result;
    result.sourceCount_ = source.entries().size();
    result.overwrittenCount_ = source.overwritten_count();
    for (const snapshot::Entry& entry : source.entries()) {
        if (matches(entry, filter)) {
            result.entries_[result.count_] = &entry;
            ++result.count_;
        }
    }
    return result;
}

} // namespace dawn::core::log::view
