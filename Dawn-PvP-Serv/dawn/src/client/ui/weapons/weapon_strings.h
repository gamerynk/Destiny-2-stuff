#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include "../../../middleware/content/packages/tables/definition_index_table.h"

namespace dawn::client::ui::weapons::strings {
template <class T> bool read(std::span<const std::byte> data, std::size_t at, T& value) noexcept {
    if (at > data.size() || sizeof(T) > data.size() - at) return false;
    std::memcpy(&value, data.data() + at, sizeof(T));
    return true;
}
inline bool relative(std::span<const std::byte> data, std::size_t at, std::size_t& out) noexcept {
    std::int64_t delta{};
    if (!read(data, at, delta) || delta < -static_cast<std::int64_t>(at)
        || delta > static_cast<std::int64_t>(data.size() - at)) return false;
    out = static_cast<std::size_t>(static_cast<std::int64_t>(at) + delta);
    return true;
}
// adapted from Sundial's investment_localization.rs (GPL-3.0-only)
// invalid tables fail before callers publish any names
bool decode(std::span<const std::byte> header, std::span<const std::byte> data,
            std::unordered_map<std::uint32_t, std::string>& output);
}
