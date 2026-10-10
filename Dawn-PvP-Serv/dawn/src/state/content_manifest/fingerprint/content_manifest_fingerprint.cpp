#include "content_manifest_fingerprint.h"

#include <Windows.h>

#include <algorithm>
#include <limits>

namespace dawn::state::content_manifest::fingerprint {
namespace {

constexpr std::byte kDomainTerminator{0};
constexpr std::byte kRowDomainVersion{1};
constexpr std::array<std::byte, 24> kRowDomain{
    std::byte{'R'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}, std::byte{'o'},
    std::byte{'r'}, std::byte{'a'}, std::byte{'t'}, std::byte{'i'}, std::byte{'o'},
    std::byte{'n'}, std::byte{'C'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'e'}, std::byte{'n'}, std::byte{'t'}, std::byte{'R'}, std::byte{'o'},
    std::byte{'w'}, std::byte{'s'}, kDomainTerminator, kRowDomainVersion,
};
constexpr std::byte kCatalogDomainVersion{1};
constexpr std::array<std::byte, 27> kCatalogDomain{
    std::byte{'R'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}, std::byte{'o'},
    std::byte{'r'}, std::byte{'a'}, std::byte{'t'}, std::byte{'i'}, std::byte{'o'},
    std::byte{'n'}, std::byte{'C'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'},
    std::byte{'e'}, std::byte{'n'}, std::byte{'t'}, std::byte{'C'}, std::byte{'a'},
    std::byte{'t'}, std::byte{'a'}, std::byte{'l'}, std::byte{'o'}, std::byte{'g'},
    kDomainTerminator, kCatalogDomainVersion,
};
constexpr std::uint8_t kUuidVersionBits = 0x80;
constexpr std::uint8_t kUuidVariantBits = 0x80;
constexpr std::uint8_t kUuidVersionPayloadMask = 0x0F;
constexpr std::uint8_t kUuidVariantPayloadMask = 0x3F;
constexpr std::size_t kUuidVersionByte = 6;
constexpr std::size_t kUuidVariantByte = 8;
constexpr std::array<std::size_t, 4> kUuidHyphenOffsets{4, 6, 8, 10};
constexpr std::string_view kHexDigits = "0123456789abcdef";

[[nodiscard]] bool succeeded(NTSTATUS status) noexcept {
    return status >= 0;
}

} // namespace

Hasher::Hasher() noexcept {
    if (!succeeded(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        algorithm_ = nullptr;
        return;
    }
    DWORD objectLength = 0;
    DWORD copied = 0;
    if (!succeeded(BCryptGetProperty(algorithm_,
                                     BCRYPT_OBJECT_LENGTH,
                                     reinterpret_cast<PUCHAR>(&objectLength),
                                     sizeof objectLength,
                                     &copied,
                                     0))
        || copied != sizeof objectLength || objectLength > object_.size()
        || !succeeded(BCryptCreateHash(algorithm_,
                                       &hash_,
                                       reinterpret_cast<PUCHAR>(object_.data()),
                                       objectLength,
                                       nullptr,
                                       0,
                                       0))) {
        BCryptCloseAlgorithmProvider(algorithm_, 0);
        algorithm_ = nullptr;
        hash_ = nullptr;
    }
}

Hasher::~Hasher() noexcept {
    if (hash_ != nullptr) {
        BCryptDestroyHash(hash_);
    }
    if (algorithm_ != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    SecureZeroMemory(object_.data(), object_.size());
}

bool Hasher::ready() const noexcept {
    return algorithm_ != nullptr && hash_ != nullptr;
}

bool Hasher::update(std::span<const std::byte> bytes) noexcept {
    if (!ready()) {
        return false;
    }
    while (!bytes.empty()) {
        const std::size_t chunkSize =
            (std::min)(bytes.size(), static_cast<std::size_t>((std::numeric_limits<ULONG>::max)()));
        if (!succeeded(
                BCryptHashData(hash_,
                               const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(bytes.data())),
                               static_cast<ULONG>(chunkSize),
                               0))) {
            return false;
        }
        bytes = bytes.subspan(chunkSize);
    }
    return true;
}

bool Hasher::finish(Fingerprint& output) noexcept {
    output = {};
    if (!ready()
        || !succeeded(BCryptFinishHash(hash_,
                                       reinterpret_cast<PUCHAR>(output.data()),
                                       static_cast<ULONG>(output.size()),
                                       0))) {
        return false;
    }
    BCryptDestroyHash(hash_);
    hash_ = nullptr;
    return true;
}

bool update_u16(Hasher& hasher, std::uint16_t value) noexcept {
    const std::array<std::byte, 2> encoded{
        static_cast<std::byte>(value & 0xFFU),
        static_cast<std::byte>((value >> 8U) & 0xFFU),
    };
    return hasher.update(encoded);
}

bool update_u32(Hasher& hasher, std::uint32_t value) noexcept {
    std::array<std::byte, 4> encoded{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        encoded[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
    return hasher.update(encoded);
}

bool update_u64(Hasher& hasher, std::uint64_t value) noexcept {
    std::array<std::byte, 8> encoded{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        encoded[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
    return hasher.update(encoded);
}

bool rows(std::span<const Row> manifestRows, Fingerprint& output) noexcept {
    output = {};
    Hasher hasher;
    if (!hasher.update(kRowDomain)
        || !update_u32(hasher, static_cast<std::uint32_t>(manifestRows.size()))) {
        return false;
    }
    for (const Row& row : manifestRows) {
        const auto nameBytes =
            std::as_bytes(std::span(row.name.data(), static_cast<std::size_t>(row.nameLength)));
        if (!update_u16(hasher, row.nameLength) || !hasher.update(nameBytes)
            || !update_u16(hasher, row.packageId) || !update_u64(hasher, row.buildSignature)) {
            return false;
        }
    }
    return hasher.finish(output);
}

bool catalog(std::span<const Row> manifestRows,
             const Fingerprint& directoryFingerprint,
             Fingerprint& output) noexcept {
    output = {};
    Fingerprint rowFingerprint{};
    Hasher hasher;
    return rows(manifestRows, rowFingerprint) && hasher.update(kCatalogDomain)
           && hasher.update(directoryFingerprint) && hasher.update(rowFingerprint)
           && hasher.finish(output);
}

void guid(const Fingerprint& buildFingerprint, Guid& output) noexcept {
    output = {};
    std::array<std::uint8_t, 16> uuid{};
    for (std::size_t index = 0; index < uuid.size(); ++index) {
        uuid[index] = std::to_integer<std::uint8_t>(buildFingerprint[index]);
    }
    uuid[kUuidVersionByte] = static_cast<std::uint8_t>(
        (uuid[kUuidVersionByte] & kUuidVersionPayloadMask) | kUuidVersionBits);
    uuid[kUuidVariantByte] = static_cast<std::uint8_t>(
        (uuid[kUuidVariantByte] & kUuidVariantPayloadMask) | kUuidVariantBits);

    std::size_t cursor = 0;
    for (std::size_t index = 0; index < uuid.size(); ++index) {
        if (std::find(kUuidHyphenOffsets.begin(), kUuidHyphenOffsets.end(), index)
            != kUuidHyphenOffsets.end()) {
            output[cursor++] = '-';
        }
        output[cursor++] = kHexDigits[uuid[index] >> 4U];
        output[cursor++] = kHexDigits[uuid[index] & 0x0FU];
    }
    output[cursor] = '\0';
}

} // namespace dawn::state::content_manifest::fingerprint
