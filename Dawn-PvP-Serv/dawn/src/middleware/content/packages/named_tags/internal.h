#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../named_tags.h"

namespace dawn::middleware::content::packages::named_tags {

struct PatchStamp {
    Visitor inner{};
    void* context{};
    std::uint32_t patchIndex{};
};

[[nodiscard]] std::uint32_t leaf_patch_index(const wchar_t* fileName) noexcept;

[[nodiscard]] bool stamp_patch(void* context, const Entry& entry) noexcept;

struct Reader {
    using Function = bool (*)(void* context,
                              std::uint64_t offset,
                              std::span<std::byte> output) noexcept;

    void* context{};
    std::uint64_t size{};
    Function read{};
};

enum class Variant : std::uint8_t {
    beta = 0,
    preBeyondLight = 1,
};

#pragma pack(push, 1)
struct PackageHeader {
    std::uint16_t version{};
    std::uint16_t platform{};
    std::uint16_t packageId{};
    std::array<std::byte, 20> beforeVariant{};
    Variant variant{};
    std::array<std::byte, 209> beforeMisc{};
    std::uint32_t miscSize{};
    std::uint32_t miscOffset{};
};

struct RelativeDirectory {
    std::uint64_t count{};
    std::uint64_t relativeOffset{};
    std::uint64_t reservedAnchor{};
};

struct BetaMiscDirectory {
    std::array<std::byte, 8> prefix{};
    RelativeDirectory namedTags{};
};

struct PreBeyondLightMiscDirectory {
    std::array<std::byte, 16> prefix{};
    RelativeDirectory namedTags{};
};

struct Record {
    std::uint32_t tag{};
    std::uint32_t classId{};
    std::uint64_t nameRelativeOffset{};
};
#pragma pack(pop)

inline constexpr std::uint16_t kSupportedVersion = 38;
inline constexpr std::uint64_t kEntryCountLimit = 8192;
inline constexpr std::size_t kPackageHeaderSize = 244;
inline constexpr std::size_t kRelativeDirectorySize = 24;
inline constexpr std::size_t kRecordSize = 16;

[[nodiscard]] bool
extract(const Reader& reader, Visitor visitor, void* context, Result& result) noexcept;

} // namespace dawn::middleware::content::packages::named_tags
