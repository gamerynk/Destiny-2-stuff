#include "queuez_update.h"

#include <limits>

#include "../encoding/byte_order.h"

namespace dawn::middleware::queuez {
namespace {

// policy cap keeps one notification below the client's whole-message failure range
constexpr std::size_t kMaximumNotificationSize = 64 * 1024;
// the client's fixed object scratch rejects payloads above 96280 bytes
constexpr std::size_t kMaximumObjectPayloadSize = 96280;
constexpr std::int32_t kNoFamilyVersion = -1;
constexpr std::size_t kRawVersionPrefixSize = encoding::kU64Size;

struct FamilyLayout {
    static constexpr std::size_t type = 0;
    static constexpr std::size_t root = type + encoding::kU32Size;
    static constexpr std::size_t version = root + encoding::kU64Size;
    static constexpr std::size_t flags = version + encoding::kU32Size;
    static constexpr std::size_t objectCount = flags + sizeof(std::uint8_t);
    static constexpr std::size_t size = objectCount + encoding::kU32Size;
};

struct ObjectLayout {
    static constexpr std::size_t id = 0;
    static constexpr std::size_t version = id + encoding::kU32Size;
    static constexpr std::size_t payloadSize = version + encoding::kU64Size;
    static constexpr std::size_t encoding = payloadSize + encoding::kU32Size;
    static constexpr std::size_t size = encoding + encoding::kU32Size;
};

constexpr std::size_t kFamilyCountSize = encoding::kU32Size;

[[nodiscard]] bool valid_encoding(Encoding value) noexcept {
    return value == Encoding::tagReflection || value == Encoding::binaryDiff
           || value == Encoding::raw || value == Encoding::oodle;
}

[[nodiscard]] bool add_object_size(const Object& object, std::size_t& total) noexcept {
    if (!valid_encoding(object.encoding) || object.payload.size() > kMaximumObjectPayloadSize
        || object.payload.size() > kMaximumNotificationSize - ObjectLayout::size
        || total > kMaximumNotificationSize - ObjectLayout::size - object.payload.size()) {
        return false;
    }
    if (!object.payload.empty() && object.encoding == Encoding::raw) {
        if (object.payload.size() < kRawVersionPrefixSize
            || encoding::read_u64_le(std::span<const std::byte, encoding::kU64Size>(
                   object.payload.data(), encoding::kU64Size))
                   != object.version) {
            return false;
        }
    }
    total += ObjectLayout::size + object.payload.size();
    return true;
}

[[nodiscard]] bool encoded_size(std::span<const Family> families, std::size_t& size) noexcept {
    size = 0;
    if (families.empty() || families.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    std::size_t total = kFamilyCountSize;
    for (const Family& family : families) {
        if (family.version == kNoFamilyVersion
            || family.objects.size() > (std::numeric_limits<std::uint32_t>::max)()
            || total > kMaximumNotificationSize - FamilyLayout::size) {
            return false;
        }
        total += FamilyLayout::size;
        for (const Object& object : family.objects) {
            if (!add_object_size(object, total)) {
                return false;
            }
        }
    }
    size = total;
    return true;
}

void write_u32(std::span<std::byte> output, std::size_t offset, std::uint32_t value) noexcept {
    encoding::write_u32_be(
        std::span<std::byte, encoding::kU32Size>(output.data() + offset, encoding::kU32Size),
        value);
}

void write_u64(std::span<std::byte> output, std::size_t offset, std::uint64_t value) noexcept {
    encoding::write_u64_be(
        std::span<std::byte, encoding::kU64Size>(output.data() + offset, encoding::kU64Size),
        value);
}

} // namespace

bool encode_update(std::span<const Family> families,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    std::size_t required = 0;
    if (!encoded_size(families, required) || output.size() < required) {
        return false;
    }

    write_u32(output, 0, static_cast<std::uint32_t>(families.size()));
    std::size_t offset = kFamilyCountSize;
    for (const Family& family : families) {
        write_u32(output, offset + FamilyLayout::type, family.type);
        write_u64(output, offset + FamilyLayout::root, family.rootSoid);
        write_u32(
            output, offset + FamilyLayout::version, static_cast<std::uint32_t>(family.version));
        output[offset + FamilyLayout::flags] = static_cast<std::byte>(family.flags);
        write_u32(output,
                  offset + FamilyLayout::objectCount,
                  static_cast<std::uint32_t>(family.objects.size()));
        offset += FamilyLayout::size;

        for (const Object& object : family.objects) {
            write_u32(output, offset + ObjectLayout::id, object.id);
            write_u64(output, offset + ObjectLayout::version, object.version);
            write_u32(output,
                      offset + ObjectLayout::payloadSize,
                      static_cast<std::uint32_t>(object.payload.size()));
            write_u32(output,
                      offset + ObjectLayout::encoding,
                      static_cast<std::uint32_t>(object.encoding));
            offset += ObjectLayout::size;
            for (const std::byte byte : object.payload) {
                output[offset++] = byte;
            }
        }
    }
    written = offset;
    return true;
}

} // namespace dawn::middleware::queuez
