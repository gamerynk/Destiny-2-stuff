#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace dawn::client::hooks::queuez::input_preferences {
// pinned 1.59 account ABI; persist only sensitivity and keyboard bindings
inline constexpr std::size_t kAccountSize = 96280;
inline constexpr std::size_t kPreferences = 2964;
inline constexpr std::size_t kBindings = 3648;
struct Values {
    std::int32_t mouse{};
    float ads{};
    std::uint8_t controller{};
    std::uint8_t source{};
    std::array<std::uint8_t, 2> reserved{};
    std::array<std::uint32_t, 60> keys{};
    bool operator==(const Values&) const = default;
};
static_assert(sizeof(Values) == 252);
inline bool valid(const Values& v) noexcept {
    return v.mouse >= 1 && v.mouse <= 100 && std::isfinite(v.ads)
        && v.ads >= .5F && v.ads <= 1.5F && v.controller <= 9 && v.source <= 1
        && v.reserved == std::array<std::uint8_t, 2>{};
}
inline Values capture_fields(std::span<const std::byte> bytes, std::size_t prefs,
    std::size_t bindings) noexcept {
    Values v{};
    if (bytes.size() < prefs + 16 || bytes.size() < bindings + 256) return v;
    std::memcpy(&v.controller, bytes.data() + prefs + 6, 1);
    std::memcpy(&v.mouse, bytes.data() + prefs + 8, 4);
    std::memcpy(&v.ads, bytes.data() + prefs + 12, 4);
    std::memcpy(&v.source, bytes.data() + bindings + 12, 1);
    std::memcpy(v.keys.data(), bytes.data() + bindings + 16, sizeof v.keys);
    return v;
}
inline bool restore_fields(std::span<std::byte> bytes, std::size_t prefs,
    std::size_t bindings, const Values& v) noexcept {
    if (bytes.size() < prefs + 16 || bytes.size() < bindings + 256 || !valid(v)) return false;
    std::memcpy(bytes.data() + prefs + 6, &v.controller, 1);
    std::memcpy(bytes.data() + prefs + 8, &v.mouse, 4);
    std::memcpy(bytes.data() + prefs + 12, &v.ads, 4);
    const std::int32_t seeded = 1;
    std::memcpy(bytes.data() + bindings, &seeded, 4);
    std::memcpy(bytes.data() + bindings + 12, &v.source, 1);
    std::memcpy(bytes.data() + bindings + 16, v.keys.data(), sizeof v.keys);
    return true;
}
inline Values capture(std::span<const std::byte> account) noexcept {
    return account.size() >= kAccountSize ? capture_fields(account, kPreferences, kBindings) : Values{};
}
inline bool restore(std::span<std::byte> account, const Values& v) noexcept {
    return account.size() >= kAccountSize && restore_fields(account, kPreferences, kBindings, v);
}
inline bool restore_owner(std::span<std::byte> owner, const Values& v) noexcept {
    return owner.size() >= 0x590 && restore_fields(owner, 0x1C, 0x2C8, v);
}
struct Document {
    std::uint32_t magic{0x50494E44};
    std::uint32_t version{1};
    std::uint64_t server{};
    std::uint64_t account{};
    Values values{};
    std::uint32_t checksum{};
};
static_assert(sizeof(Document) == 280);
inline std::uint32_t checksum(const Document& d) noexcept {
    std::uint32_t hash = 2166136261U;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&d);
    for (std::size_t i = 0; i < offsetof(Document, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
}
inline bool valid(const Document& d, std::uint64_t server, std::uint64_t account) noexcept {
    return account != 0 && d.magic == 0x50494E44 && d.version == 1
        && d.server == server && d.account == account && valid(d.values)
        && d.checksum == checksum(d);
}
} // namespace dawn::client::hooks::queuez::input_preferences
