#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::queuez {

enum class Encoding : std::uint32_t {
    tagReflection = 1,
    binaryDiff = 2,
    raw = 3,
    oodle = 4,
};

struct Object {
    std::uint32_t id{};
    std::uint64_t version{};
    Encoding encoding{};
    std::span<const std::byte> payload{};
};

struct Family {
    std::uint32_t type{};
    std::uint64_t rootSoid{};
    std::int32_t version{};
    std::uint8_t flags{};
    std::span<const Object> objects{};
};

inline constexpr std::uint8_t kFullSnapshotFlag = 1;

[[nodiscard]] bool encode_update(std::span<const Family> families,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept;

} // namespace dawn::middleware::queuez
