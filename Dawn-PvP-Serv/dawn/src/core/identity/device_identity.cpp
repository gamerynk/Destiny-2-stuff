#include "device_identity.h"

#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

#include "../filesystem/path.h"

namespace dawn::core::identity::device_identity {
namespace {

constexpr std::wstring_view kKeyFileSuffix = L"\\device_identity.key";
constexpr std::array<std::byte, 8> kMagic{
    std::byte{'R'}, std::byte{'S'}, std::byte{'T'}, std::byte{'D'},
    std::byte{'V'}, std::byte{'1'}, std::byte{0}, std::byte{0},
};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaximumAgeSeconds = 15U * 60U;
constexpr std::uint64_t kFutureSkewSeconds = 5U * 60U;
constexpr std::size_t kPrivateKeySize = 104;
constexpr std::size_t kSignatureSize = 64;
constexpr std::size_t kHashSize = 32;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kScopeOffset = 12;
constexpr std::size_t kPlatformIdOffset = 16;
constexpr std::size_t kIssuedAtOffset = 24;
constexpr std::size_t kNonceOffset = 32;
constexpr std::size_t kNonceSize = 32;
constexpr std::size_t kPublicKeyOffset = kNonceOffset + kNonceSize;
constexpr std::size_t kSignatureOffset = kPublicKeyOffset + kPublicKeySize;
static_assert(kSignatureOffset + kSignatureSize == kProofSize);
constexpr std::uint64_t kSteamIndividualPrefix = 0x0110000100000000ULL;
constexpr std::array<std::uint32_t, 3> kReservedAccountIds{
    0x00000001U,
    0x00000002U,
    0x00000003U,
};
constexpr std::array<std::byte, 25> kDpapiEntropy{
    std::byte{'R'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}, std::byte{'o'},
    std::byte{'r'}, std::byte{'a'}, std::byte{'t'}, std::byte{'i'}, std::byte{'o'},
    std::byte{'n'}, std::byte{'-'}, std::byte{'D'}, std::byte{'e'}, std::byte{'v'},
    std::byte{'i'}, std::byte{'c'}, std::byte{'e'}, std::byte{'-'}, std::byte{'K'},
    std::byte{'e'}, std::byte{'y'}, std::byte{'-'}, std::byte{'v'}, std::byte{'1'},
};

SRWLOCK g_lock{SRWLOCK_INIT};
BCRYPT_ALG_HANDLE g_algorithm{};
BCRYPT_KEY_HANDLE g_key{};
std::array<std::byte, kPublicKeySize> g_publicKey{};
std::uint64_t g_platformId{};
bool g_ready{};

template <typename Value>
void write_integer(std::span<std::byte, kProofSize> output,
                   std::size_t offset,
                   Value value) noexcept {
    static_assert(std::is_integral_v<Value>);
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        output[offset + index] = static_cast<std::byte>(bits & static_cast<Unsigned>(0xFFU));
        bits >>= 8U;
    }
}

template <typename Value>
[[nodiscard]] Value read_integer(std::span<const std::byte, kProofSize> input,
                                 std::size_t offset) noexcept {
    static_assert(std::is_integral_v<Value>);
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned bits = 0;
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        bits |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(input[offset + index]))
                << static_cast<unsigned>(index * 8U);
    }
    return static_cast<Value>(bits);
}

[[nodiscard]] bool randomize(std::span<std::byte> output) noexcept {
    return output.size() <= static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())
           && BCryptGenRandom(nullptr,
                              reinterpret_cast<PUCHAR>(output.data()),
                              static_cast<ULONG>(output.size()),
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG)
                  >= 0;
}

[[nodiscard]] bool sha256(std::span<const std::byte> input,
                          std::span<std::byte, kHashSize> output) noexcept {
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    const bool success = BCryptOpenAlgorithmProvider(
                             &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)
                             >= 0
                         && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
                         && BCryptHashData(hash,
                                           reinterpret_cast<PUCHAR>(
                                               const_cast<std::byte*>(input.data())),
                                           static_cast<ULONG>(input.size()),
                                           0)
                                >= 0
                         && BCryptFinishHash(hash,
                                             reinterpret_cast<PUCHAR>(output.data()),
                                             static_cast<ULONG>(output.size()),
                                             0)
                                >= 0;
    if (hash != nullptr) {
        (void)BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return success;
}

[[nodiscard]] bool reserved_account_id(std::uint32_t value) noexcept {
    return value == 0
           || std::find(kReservedAccountIds.cbegin(), kReservedAccountIds.cend(), value)
                  != kReservedAccountIds.cend();
}

[[nodiscard]] bool valid_platform_id(std::uint64_t value) noexcept {
    return (value & 0xFFFFFFFF00000000ULL) == kSteamIndividualPrefix
           && static_cast<std::uint32_t>(value) != 0;
}

[[nodiscard]] std::uint64_t derive_platform_id(
    std::span<const std::byte, kPublicKeySize> publicKey) noexcept {
    std::array<std::byte, kHashSize> digest{};
    if (!sha256(publicKey, digest)) {
        return 0;
    }
    std::uint32_t accountId = 0;
    for (std::size_t offset = 0; offset + sizeof(accountId) <= digest.size();
         offset += sizeof(accountId)) {
        std::memcpy(&accountId, digest.data() + offset, sizeof(accountId));
        if (!reserved_account_id(accountId)) {
            break;
        }
        accountId = 0;
    }
    SecureZeroMemory(digest.data(), digest.size());
    return accountId == 0 ? 0 : kSteamIndividualPrefix | accountId;
}

[[nodiscard]] bool export_key(BCRYPT_KEY_HANDLE key,
                              LPCWSTR type,
                              std::span<std::byte> output) noexcept {
    ULONG required = 0;
    ULONG written = 0;
    return key != nullptr
           && BCryptExportKey(key, nullptr, type, nullptr, 0, &required, 0) >= 0
           && required == output.size()
           && BCryptExportKey(key,
                              nullptr,
                              type,
                              reinterpret_cast<PUCHAR>(output.data()),
                              static_cast<ULONG>(output.size()),
                              &written,
                              0)
                  >= 0
           && written == output.size();
}

[[nodiscard]] bool read_file(const path::Buffer& keyPath,
                             std::array<std::byte, 4096>& encrypted,
                             DWORD& encryptedSize) noexcept {
    encryptedSize = 0;
    const HANDLE file = CreateFileW(keyPath.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    const bool success = GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0
                         && size.QuadPart <= static_cast<LONGLONG>(encrypted.size())
                         && ReadFile(file,
                                     encrypted.data(),
                                     static_cast<DWORD>(size.QuadPart),
                                     &encryptedSize,
                                     nullptr)
                                != FALSE
                         && encryptedSize == static_cast<DWORD>(size.QuadPart);
    (void)CloseHandle(file);
    return success;
}

[[nodiscard]] bool unprotect_private_key(const path::Buffer& keyPath,
                                         std::span<std::byte, kPrivateKeySize> output) noexcept {
    std::array<std::byte, 4096> encrypted{};
    DWORD encryptedSize = 0;
    if (!read_file(keyPath, encrypted, encryptedSize)) {
        return false;
    }
    DATA_BLOB input{encryptedSize, reinterpret_cast<BYTE*>(encrypted.data())};
    DATA_BLOB entropy{static_cast<DWORD>(kDpapiEntropy.size()),
                      reinterpret_cast<BYTE*>(const_cast<std::byte*>(kDpapiEntropy.data()))};
    DATA_BLOB clear{};
    const bool success = CryptUnprotectData(
                             &input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &clear)
                             != FALSE
                         && clear.cbData == output.size() && clear.pbData != nullptr;
    if (success) {
        std::memcpy(output.data(), clear.pbData, output.size());
    }
    if (clear.pbData != nullptr) {
        SecureZeroMemory(clear.pbData, clear.cbData);
        (void)LocalFree(clear.pbData);
    }
    SecureZeroMemory(encrypted.data(), encrypted.size());
    return success;
}

[[nodiscard]] bool protect_and_store_private_key(
    const path::Buffer& keyPath,
    std::span<const std::byte, kPrivateKeySize> privateKey) noexcept {
    DATA_BLOB input{static_cast<DWORD>(privateKey.size()),
                    reinterpret_cast<BYTE*>(const_cast<std::byte*>(privateKey.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(kDpapiEntropy.size()),
                      reinterpret_cast<BYTE*>(const_cast<std::byte*>(kDpapiEntropy.data()))};
    DATA_BLOB encrypted{};
    if (CryptProtectData(&input,
                         L"Restoration device identity",
                         &entropy,
                         nullptr,
                         nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN,
                         &encrypted)
            == FALSE
        || encrypted.pbData == nullptr || encrypted.cbData == 0) {
        return false;
    }
    const HANDLE file = CreateFileW(keyPath.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        SecureZeroMemory(encrypted.pbData, encrypted.cbData);
        (void)LocalFree(encrypted.pbData);
        return false;
    }
    DWORD written = 0;
    bool success = WriteFile(file,
                             encrypted.pbData,
                             encrypted.cbData,
                             &written,
                             nullptr)
                       != FALSE
                   && written == encrypted.cbData && FlushFileBuffers(file) != FALSE;
    success = CloseHandle(file) != FALSE && success;
    if (!success) {
        (void)DeleteFileW(keyPath.chars.data());
    }
    SecureZeroMemory(encrypted.pbData, encrypted.cbData);
    (void)LocalFree(encrypted.pbData);
    return success;
}

[[nodiscard]] bool import_private(BCRYPT_ALG_HANDLE algorithm,
                                  std::span<const std::byte, kPrivateKeySize> privateKey,
                                  BCRYPT_KEY_HANDLE& key) noexcept {
    return BCryptImportKeyPair(algorithm,
                               nullptr,
                               BCRYPT_ECCPRIVATE_BLOB,
                               &key,
                               reinterpret_cast<PUCHAR>(
                                   const_cast<std::byte*>(privateKey.data())),
                               static_cast<ULONG>(privateKey.size()),
                               0)
           >= 0;
}

[[nodiscard]] bool create_key(BCRYPT_ALG_HANDLE algorithm,
                              const path::Buffer& keyPath,
                              BCRYPT_KEY_HANDLE& key) noexcept {
    std::array<std::byte, kPrivateKeySize> privateKey{};
    bool success = BCryptGenerateKeyPair(algorithm, &key, 256, 0) >= 0
                   && BCryptFinalizeKeyPair(key, 0) >= 0
                   && export_key(key, BCRYPT_ECCPRIVATE_BLOB, privateKey)
                   && protect_and_store_private_key(keyPath, privateKey);
    SecureZeroMemory(privateKey.data(), privateKey.size());
    if (!success && key != nullptr) {
        (void)BCryptDestroyKey(key);
        key = nullptr;
    }
    return success;
}

[[nodiscard]] bool load_or_create_key(BCRYPT_ALG_HANDLE algorithm,
                                      const path::Buffer& keyPath,
                                      BCRYPT_KEY_HANDLE& key) noexcept {
    std::array<std::byte, kPrivateKeySize> privateKey{};
    if (unprotect_private_key(keyPath, privateKey)) {
        const bool imported = import_private(algorithm, privateKey, key);
        SecureZeroMemory(privateKey.data(), privateKey.size());
        return imported;
    }
    SecureZeroMemory(privateKey.data(), privateKey.size());
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        return false;
    }
    return create_key(algorithm, keyPath, key);
}

} // namespace

bool initialize(void* module) noexcept {
    return initialize(module, 0);
}

bool initialize(void* module, std::uint64_t requestedPlatformId) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    path::Buffer keyPath;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_KEY_HANDLE key{};
    std::array<std::byte, kPublicKeySize> publicKey{};
    const bool success = path::artifact_directory(module, keyPath)
                         && path::append(keyPath, kKeyFileSuffix)
                         && BCryptOpenAlgorithmProvider(
                                &algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)
                                >= 0
                         && load_or_create_key(algorithm, keyPath, key)
                         && export_key(key, BCRYPT_ECCPUBLIC_BLOB, publicKey);
    const std::uint64_t deviceId = success ? derive_platform_id(publicKey) : 0;
    const std::uint64_t platformId = requestedPlatformId != 0 ? requestedPlatformId : deviceId;
    if (success && deviceId != 0 && valid_platform_id(platformId)) {
        g_algorithm = algorithm;
        g_key = key;
        g_publicKey = publicKey;
        g_platformId = platformId;
        g_ready = true;
    } else {
        if (key != nullptr) {
            (void)BCryptDestroyKey(key);
        }
        if (algorithm != nullptr) {
            (void)BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success && deviceId != 0 && valid_platform_id(platformId);
}

std::uint64_t platform_id() noexcept {
    AcquireSRWLockShared(&g_lock);
    const std::uint64_t result = g_ready ? g_platformId : 0;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

bool issue(Scope scope,
           std::uint64_t nowSeconds,
           std::span<std::byte, kProofSize> output) noexcept {
    if (nowSeconds == 0 || scope < Scope::enroll || scope > Scope::transportCertificate) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!g_ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    std::fill(output.begin(), output.end(), std::byte{0});
    std::copy(kMagic.begin(), kMagic.end(), output.begin() + kMagicOffset);
    write_integer(output, kVersionOffset, kVersion);
    write_integer(output, kScopeOffset, static_cast<std::uint32_t>(scope));
    write_integer(output, kPlatformIdOffset, g_platformId);
    write_integer(output, kIssuedAtOffset, nowSeconds);
    std::copy(g_publicKey.begin(), g_publicKey.end(), output.begin() + kPublicKeyOffset);
    std::array<std::byte, kHashSize> digest{};
    ULONG signatureSize = 0;
    const bool success = randomize(output.subspan<kNonceOffset, kNonceSize>())
                         && sha256(output.first<kSignatureOffset>(), digest)
                         && BCryptSignHash(g_key,
                                           nullptr,
                                           reinterpret_cast<PUCHAR>(digest.data()),
                                           static_cast<ULONG>(digest.size()),
                                           reinterpret_cast<PUCHAR>(
                                               output.data() + kSignatureOffset),
                                           static_cast<ULONG>(kSignatureSize),
                                           &signatureSize,
                                           0)
                                >= 0
                         && signatureSize == kSignatureSize;
    SecureZeroMemory(digest.data(), digest.size());
    if (!success) {
        SecureZeroMemory(output.data(), output.size());
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool verify(Scope expectedScope,
            std::span<const std::byte> input,
            std::uint64_t nowSeconds,
            Claims& claims) noexcept {
    claims = {};
    if (input.size() < kProofSize || nowSeconds == 0) {
        return false;
    }
    for (std::size_t offset = 0; offset <= input.size() - kProofSize; ++offset) {
        if (input[offset] != kMagic.front()) {
            continue;
        }
        const auto proof = std::span<const std::byte, kProofSize>(input.data() + offset,
                                                                 kProofSize);
        if (!std::equal(kMagic.begin(), kMagic.end(), proof.begin() + kMagicOffset)
            || read_integer<std::uint32_t>(proof, kVersionOffset) != kVersion
            || read_integer<std::uint32_t>(proof, kScopeOffset)
                   != static_cast<std::uint32_t>(expectedScope)) {
            continue;
        }
        const std::uint64_t issuedAt = read_integer<std::uint64_t>(proof, kIssuedAtOffset);
        if (issuedAt > nowSeconds + kFutureSkewSeconds || nowSeconds > issuedAt + kMaximumAgeSeconds) {
            continue;
        }
        std::array<std::byte, kPublicKeySize> publicKey{};
        std::copy_n(proof.data() + kPublicKeyOffset, publicKey.size(), publicKey.begin());
        const std::uint64_t platformId = read_integer<std::uint64_t>(proof, kPlatformIdOffset);
        const std::uint64_t deviceId = derive_platform_id(publicKey);
        if (!valid_platform_id(platformId) || deviceId == 0) {
            continue;
        }
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_KEY_HANDLE key{};
        std::array<std::byte, kHashSize> digest{};
        const bool valid = BCryptOpenAlgorithmProvider(
                               &algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)
                               >= 0
                           && BCryptImportKeyPair(
                                  algorithm,
                                  nullptr,
                                  BCRYPT_ECCPUBLIC_BLOB,
                                  &key,
                                  reinterpret_cast<PUCHAR>(publicKey.data()),
                                  static_cast<ULONG>(publicKey.size()),
                                  0)
                                  >= 0
                           && sha256(proof.first<kSignatureOffset>(), digest)
                           && BCryptVerifySignature(
                                  key,
                                  nullptr,
                                  reinterpret_cast<PUCHAR>(digest.data()),
                                  static_cast<ULONG>(digest.size()),
                                  reinterpret_cast<PUCHAR>(const_cast<std::byte*>(
                                      proof.data() + kSignatureOffset)),
                                  static_cast<ULONG>(kSignatureSize),
                                  0)
                                  >= 0;
        SecureZeroMemory(digest.data(), digest.size());
        if (key != nullptr) {
            (void)BCryptDestroyKey(key);
        }
        if (algorithm != nullptr) {
            (void)BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        if (valid) {
            claims.deviceId = deviceId;
            claims.platformId = platformId;
            claims.issuedAt = issuedAt;
            claims.publicKey = publicKey;
            return true;
        }
    }
    return false;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_key != nullptr) {
        (void)BCryptDestroyKey(g_key);
    }
    if (g_algorithm != nullptr) {
        (void)BCryptCloseAlgorithmProvider(g_algorithm, 0);
    }
    g_key = nullptr;
    g_algorithm = nullptr;
    SecureZeroMemory(g_publicKey.data(), g_publicKey.size());
    g_platformId = 0;
    g_ready = false;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::core::identity::device_identity
