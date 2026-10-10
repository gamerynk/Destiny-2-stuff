#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../../protobuf/codec.h"
#include "encoder.h"

namespace dawn::middleware::content::manifest {
namespace {

constexpr std::uint32_t kEntitlementsField = 2;
constexpr std::uint32_t kPackageRowsField = 3;
constexpr std::uint32_t kGuidField = 5;
constexpr std::uint32_t kOfferKeyField = 1;
constexpr std::uint32_t kPackageNameField = 2;
constexpr std::uint32_t kPackageIdField = 3;
// MsgC field 4 is the exact public build signature from header offset +0x08
constexpr std::uint32_t kBuildSignatureField = 4;
constexpr std::size_t kEncodedRowCapacity = 288;
constexpr std::size_t kEncodedEntitlementCapacity = 96;
constexpr std::array<std::size_t, 4> kGuidHyphens{8, 13, 18, 23};

[[nodiscard]] bool is_lower_hex(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

// the Client compares 37 bytes with the token from fetch time; a shorter id makes
// that read past the string
[[nodiscard]] bool valid_guid(std::string_view guid) noexcept {
    if (guid.size() != state::content_manifest::kGuidCapacity - 1) {
        return false;
    }
    for (std::size_t index = 0; index < guid.size(); ++index) {
        bool hyphen = false;
        for (const std::size_t offset : kGuidHyphens) {
            hyphen = hyphen || index == offset;
        }
        if ((hyphen && guid[index] != '-') || (!hyphen && !is_lower_hex(guid[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool encode_row(const state::content_manifest::Row& row,
                              std::span<std::byte> output,
                              std::size_t& size) noexcept {
    size = 0;
    protobuf::Writer writer(output);
    const std::size_t nameLength =
        (std::min)(static_cast<std::size_t>(row.nameLength), row.name.size());
    const auto name = std::as_bytes(std::span(row.name.data(), nameLength));
    if (!writer.write_varint(kOfferKeyField, state::entitlements::kOfferKey)
        || !writer.write_length_delimited(kPackageNameField, name)
        || !writer.write_varint(kPackageIdField, row.packageId)
        || !writer.write_varint(kBuildSignatureField, row.buildSignature)) {
        return false;
    }
    size = writer.size();
    return true;
}

} // namespace

bool encode(const state::entitlements::Table& entitlements,
            std::span<const state::content_manifest::Row> rows,
            std::string_view guid,
            std::span<std::byte> output,
            std::size_t& size) noexcept {
    size = 0;
    if (!valid_guid(guid)) {
        return false;
    }
    protobuf::Writer writer(output);
    const std::size_t definitionCount = (std::min)(entitlements.count, entitlements.entries.size());
    const std::span<const state::entitlements::Entitlement> definitions =
        std::span(entitlements.entries).first(definitionCount);
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        std::array<std::byte, kEncodedEntitlementCapacity> nested{};
        std::size_t nestedSize = 0;
        if (!encode_entitlement(
                definitions[index], state::entitlements::handle(index), nested, nestedSize)
            || !writer.write_length_delimited(kEntitlementsField,
                                              std::span(nested).first(nestedSize))) {
            return false;
        }
    }
    for (const state::content_manifest::Row& row : rows) {
        std::array<std::byte, kEncodedRowCapacity> nested{};
        std::size_t nestedSize = 0;
        if (!encode_row(row, nested, nestedSize)
            || !writer.write_length_delimited(kPackageRowsField,
                                              std::span(nested).first(nestedSize))) {
            return false;
        }
    }
    const auto guidBytes = std::as_bytes(std::span(guid.data(), guid.size()));
    if (!writer.write_length_delimited(kGuidField, guidBytes)) {
        return false;
    }
    size = writer.size();
    return true;
}

} // namespace dawn::middleware::content::manifest
