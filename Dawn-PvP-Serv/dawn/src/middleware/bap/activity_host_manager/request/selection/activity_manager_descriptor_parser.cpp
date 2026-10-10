#include "internal.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {
namespace {

using encoding::bits::Reader;

constexpr std::uint8_t kReasonWidth = 4;
constexpr std::uint8_t kActivityIndexWidth = 12;
constexpr std::uint8_t kElementIndexWidth = 9;
constexpr std::int16_t kScalarBias = 1;
constexpr std::size_t kSensitiveScalarWidth = 64;
// 5-bit count can declare more skulls than storage holds
constexpr std::uint8_t kSkullCountWidth = 5;
constexpr std::uint8_t kSkullGroupWidth = 2;
constexpr std::uint8_t kSkullValueWidth = 7;
constexpr std::size_t kUnknownByteWidth = 8;
constexpr std::uint8_t kPackageNameWidth = 8;
constexpr std::int16_t kPackageNameBias = 128;
constexpr std::int8_t kLowercaseMinimum = 'a';
constexpr std::int8_t kLowercaseMaximum = 'z';
constexpr std::int8_t kDigitMinimum = '0';
constexpr std::int8_t kDigitMaximum = '9';
constexpr std::int8_t kUnderscore = '_';
constexpr std::int8_t kPackageNamePadding = 0;
constexpr std::uint8_t kEmptyPackageNameLength = 0;
constexpr std::size_t kUnknownHashWidth = 32;
constexpr std::size_t kBooleanWidth = 1;
constexpr std::uint8_t kReferenceCountWidth = 1;
constexpr std::size_t kReferenceEntryWidth = 96;
constexpr std::uint8_t kPairCountWidth = 6;

[[nodiscard]] bool is_package_name_character(std::int8_t value) noexcept {
    return (value >= kLowercaseMinimum && value <= kLowercaseMaximum)
           || (value >= kDigitMinimum && value <= kDigitMaximum) || value == kUnderscore;
}

[[nodiscard]] bool read_element_index(Reader& reader,
                                      ActivityManagerSelection& selection) noexcept {
    bool present = false;
    if (!read_presence(reader, present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    std::int16_t value = 0;
    if (!read_i16(reader, kElementIndexWidth, kScalarBias, value)) {
        return false;
    }
    selection.hasElementIndex = true;
    selection.elementIndex = value;
    return true;
}

// count reaches 31 but storage holds 16; read every entry's bits so later fields stay
// aligned
[[nodiscard]] bool read_skulls(Reader& reader, ActivityManagerSelection& selection) noexcept {
    std::uint64_t encodedCount = 0;
    if (!reader.read(kSkullCountWidth, encodedCount)) {
        return false;
    }
    selection.skullCount = static_cast<std::uint8_t>(
        (std::min)(encodedCount, static_cast<std::uint64_t>(kActivityManagerSkullCapacity)));
    for (std::uint64_t index = 0; index < encodedCount; ++index) {
        ActivityManagerSkullValue value{};
        if (!read_i8(reader, kSkullGroupWidth, kScalarBias, value.group)
            || !read_i8(reader, kSkullValueWidth, kScalarBias, value.value)) {
            return false;
        }
        if (index < selection.skullCount) {
            selection.skulls[index] = value;
        }
    }
    return true;
}

// name ends at the first byte the text rule rejects (the field is always 40, tail is
// padding) instead of failing the descriptor and losing the destination choice
[[nodiscard]] bool read_package_name(Reader& reader,
                                     std::size_t descriptorStart,
                                     ActivityManagerSelection& selection) noexcept {
    bool present = false;
    if (!read_presence(reader, present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    // a forced destination rewrites these bytes in place, so keep where they sit
    const std::size_t nameStart = descriptorStart - reader.remaining_bits();
    bool ended = false;
    std::uint8_t length = 0;
    for (std::int8_t& character : selection.packageName) {
        std::int8_t decoded = kPackageNamePadding;
        if (!read_i8(reader, kPackageNameWidth, kPackageNameBias, decoded)) {
            return false;
        }
        ended = ended || !is_package_name_character(decoded);
        character = ended ? kPackageNamePadding : decoded;
        length = ended ? length : static_cast<std::uint8_t>(length + 1);
    }
    if (length == kEmptyPackageNameLength) {
        return true;
    }
    selection.hasPackageName = true;
    selection.packageNameLength = length;
    selection.packageNameBitOffset = nameStart;
    return true;
}

[[nodiscard]] bool read_prefix(Reader& reader, ActivityManagerSelection& selection) noexcept {
    return read_i8(reader, kReasonWidth, kScalarBias, selection.reason)
           && read_i16(reader, kActivityIndexWidth, kScalarBias, selection.sourceActivityIndex)
           && read_i16(reader, kActivityIndexWidth, kScalarBias, selection.activityIndex)
           && read_element_index(reader, selection) && skip_optional(reader, kSensitiveScalarWidth)
           && skip_optional(reader, kSensitiveScalarWidth) && read_skulls(reader, selection);
}

[[nodiscard]] bool read_destination(Reader& reader,
                                    std::size_t descriptorStart,
                                    ActivityManagerSelection& selection) noexcept {
    return reader.skip(kUnknownByteWidth) && skip_optional(reader, kUnknownByteWidth)
           && read_hash(reader, selection.hasArrivalBubbleHash, selection.arrivalBubbleHash)
           && read_hash(reader, selection.hasSpawnSetHash, selection.spawnSetHash)
           && read_package_name(reader, descriptorStart, selection)
           && skip_optional(reader, kUnknownHashWidth) && reader.skip(kBooleanWidth);
}

// all six-bit counts are kept; the native 32-entry limit is checked by the rules resolver
[[nodiscard]] bool read_tail(Reader& reader, ActivityManagerSelection& selection) noexcept {
    bool hasReferences = false;
    std::uint64_t referenceCount = 0;
    if (!read_presence(reader, hasReferences)
        || (hasReferences
            && (!reader.read(kReferenceCountWidth, referenceCount)
                || !reader.skip(static_cast<std::size_t>(referenceCount)
                                * kReferenceEntryWidth)))) {
        return false;
    }

    bool hasPairs = false;
    std::uint64_t pairCount = 0;
    if (!read_presence(reader, hasPairs)) {
        return false;
    }
    if (!hasPairs) {
        return true;
    }
    if (!reader.read(kPairCountWidth, pairCount) || pairCount>selection.menuOptions.size()) return false;
    selection.menuOptionCount=static_cast<std::uint8_t>(pairCount);
    // native schema 0x8080772C: option definition index (9 bits), choice (4), both bias 1
    for (std::size_t i=0;i<pairCount;++i)
        if (!read_i16(reader,9,1,selection.menuOptions[i].index)
            || !read_i16(reader,4,1,selection.menuOptions[i].choice)) return false;
    return true;
}

} // namespace

bool parse(Reader& reader, ActivityManagerSelection& selection) noexcept {
    const std::size_t descriptorStart = reader.remaining_bits();
    return read_prefix(reader, selection) && read_destination(reader, descriptorStart, selection)
           && read_tail(reader, selection);
}

} // namespace
