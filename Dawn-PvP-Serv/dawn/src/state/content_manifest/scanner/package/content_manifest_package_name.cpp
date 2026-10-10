#include "content_manifest_package_name.h"

#include <algorithm>
#include <limits>

namespace dawn::state::content_manifest::scanner::package {
namespace {

// patch index is one unsigned byte in this client build
constexpr std::uint32_t kMaximumPatchIndex = 0xFF;
constexpr std::uint32_t kDecimalRadix = 10;
constexpr std::size_t kPackageIdHexLength = 4;
constexpr std::uint8_t kHexRadix = 16;
constexpr std::uint32_t kBitsPerHexDigit = 4;
constexpr std::size_t kMinimumLocaleLength = 2;
constexpr std::size_t kMaximumLocaleLength = 3;
constexpr std::string_view kPackageExtension = ".pkg";
// _unp1_ marks packages registered locally before ContentConfig is read
constexpr std::string_view kUnpatchableMarker = "_unp1_";

[[nodiscard]] char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] bool is_decimal(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_letter(char value) noexcept {
    const char lowered = ascii_lower(value);
    return lowered >= 'a' && lowered <= 'z';
}

[[nodiscard]] std::uint8_t hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    const char lowered = ascii_lower(value);
    return lowered >= 'a' && lowered <= 'f' ? static_cast<std::uint8_t>(lowered - 'a' + 10)
                                            : kHexRadix;
}

[[nodiscard]] bool parse_patch(std::string_view text, std::uint32_t& value) noexcept {
    value = 0;
    if (text.empty()) {
        return false;
    }
    for (const char character : text) {
        if (!is_decimal(character)) {
            return false;
        }
        const std::uint32_t digit = static_cast<std::uint32_t>(character - '0');
        if (value > (kMaximumPatchIndex - digit) / kDecimalRadix) {
            return false;
        }
        value = value * kDecimalRadix + digit;
    }
    return true;
}

[[nodiscard]] bool parse_package_id(std::string_view text, std::uint16_t& value) noexcept {
    value = 0;
    if (text.size() != kPackageIdHexLength) {
        return false;
    }
    for (const char character : text) {
        const std::uint8_t digit = hex_value(character);
        if (digit >= kHexRadix) {
            return false;
        }
        value = static_cast<std::uint16_t>((value << kBitsPerHexDigit) | digit);
    }
    return value >= kMinimumPackageId && value <= kMaximumPackageId;
}

[[nodiscard]] bool is_locale(std::string_view text) noexcept {
    return (text.size() == kMinimumLocaleLength || text.size() == kMaximumLocaleLength)
           && std::all_of(text.begin(), text.end(), is_letter);
}

[[nodiscard]] bool looks_patchable(std::string_view stem) noexcept {
    const std::size_t patchSeparator = stem.rfind('_');
    if (patchSeparator == std::string_view::npos || patchSeparator == 0) {
        return false;
    }
    const std::string_view patch = stem.substr(patchSeparator + 1);
    if (patch.empty() || !std::all_of(patch.begin(), patch.end(), is_decimal)) {
        return false;
    }
    const std::string_view identity = stem.substr(0, patchSeparator);
    const std::size_t lastSeparator = identity.rfind('_');
    if (lastSeparator == std::string_view::npos || lastSeparator == 0) {
        return false;
    }
    const std::string_view tail = identity.substr(lastSeparator + 1);
    if (tail.size() == kPackageIdHexLength) {
        return true;
    }
    const std::string_view withoutLocale = identity.substr(0, lastSeparator);
    const std::size_t idSeparator = withoutLocale.rfind('_');
    return is_locale(tail) && idSeparator != std::string_view::npos && idSeparator != 0
           && withoutLocale.size() - idSeparator - 1 == kPackageIdHexLength;
}

[[nodiscard]] bool valid_prefix(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char value) {
        return is_letter(value) || is_decimal(value) || value == '_';
    });
}

[[nodiscard]] bool parse_canonical(std::string_view stem, ParsedName& output) noexcept {
    output = {};
    if (stem.empty() || stem.size() >= output.stem.size()
        || stem.find(kUnpatchableMarker) != std::string_view::npos) {
        return false;
    }
    const std::size_t patchSeparator = stem.rfind('_');
    if (patchSeparator == std::string_view::npos || patchSeparator == 0) {
        return false;
    }
    if (!parse_patch(stem.substr(patchSeparator + 1), output.patchIndex)) {
        return false;
    }

    const std::string_view identity = stem.substr(0, patchSeparator);
    const std::size_t lastSeparator = identity.rfind('_');
    if (lastSeparator == std::string_view::npos || lastSeparator == 0) {
        return false;
    }
    std::size_t idSeparator = lastSeparator;
    std::string_view idText = identity.substr(lastSeparator + 1);
    if (is_locale(idText)) {
        const std::string_view withoutLocale = identity.substr(0, lastSeparator);
        idSeparator = withoutLocale.rfind('_');
        if (idSeparator == std::string_view::npos || idSeparator == 0) {
            return false;
        }
        idText = withoutLocale.substr(idSeparator + 1);
    }
    if (!valid_prefix(identity.substr(0, idSeparator))
        || !parse_package_id(idText, output.packageId)) {
        return false;
    }

    std::copy(stem.begin(), stem.end(), output.stem.begin());
    output.stemLength = static_cast<std::uint16_t>(stem.size());
    output.identityLength = static_cast<std::uint16_t>(identity.size());
    return true;
}

} // namespace

NameStatus parse_file_name(std::wstring_view fileName, ParsedName& output) noexcept {
    output = {};
    if (fileName.size() <= kPackageExtension.size()) {
        return NameStatus::unrecognized;
    }

    std::array<char, kPackageNameCapacity + kPackageExtension.size()> canonical{};
    if (fileName.size() >= canonical.size()) {
        return NameStatus::invalid;
    }
    for (std::size_t index = 0; index < fileName.size(); ++index) {
        const wchar_t character = fileName[index];
        if (character > static_cast<wchar_t>((std::numeric_limits<unsigned char>::max)())) {
            return NameStatus::invalid;
        }
        canonical[index] = ascii_lower(static_cast<char>(character));
    }
    const std::string_view converted(canonical.data(), fileName.size());
    if (!converted.ends_with(kPackageExtension)) {
        return NameStatus::unrecognized;
    }

    const std::string_view stem = converted.substr(0, converted.size() - kPackageExtension.size());
    if (stem.find(kUnpatchableMarker) != std::string_view::npos) {
        return NameStatus::excluded;
    }
    if (!looks_patchable(stem)) {
        return NameStatus::unrecognized;
    }
    return parse_canonical(stem, output) ? NameStatus::recognized : NameStatus::invalid;
}

bool parse_stem(std::string_view stem, ParsedName& output) noexcept {
    if (std::any_of(
            stem.begin(), stem.end(), [](char value) { return ascii_lower(value) != value; })) {
        output = {};
        return false;
    }
    return looks_patchable(stem) && parse_canonical(stem, output);
}

} // namespace dawn::state::content_manifest::scanner::package
