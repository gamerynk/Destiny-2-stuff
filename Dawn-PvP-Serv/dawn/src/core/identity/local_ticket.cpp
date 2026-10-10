#include "local_ticket.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

#include "../filesystem/path.h"

namespace dawn::core::identity::local_ticket {
namespace {

constexpr std::wstring_view kKeyFileSuffix = L"\\identity.key";
constexpr std::size_t kKeySize = 32;
constexpr std::size_t kMacSize = 32;
constexpr std::size_t kMacOffset = kTicketSize - kMacSize;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kProviderOffset = 12;
constexpr std::size_t kPlatformIdOffset = 16;
constexpr std::size_t kIssuedAtOffset = 24;
constexpr std::size_t kExpiresAtOffset = 32;
constexpr std::size_t kNonceOffset = 40;
constexpr std::size_t kNonceSize = 32;
constexpr std::size_t kRandomPaddingOffset = kNonceOffset + kNonceSize;
constexpr std::size_t kRandomPaddingSize = kMacOffset - kRandomPaddingOffset;
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint64_t kLifetimeSeconds = 15U * 60U;
constexpr std::uint64_t kFutureSkewSeconds = 5U * 60U;
constexpr std::array<std::byte, 8> kMagic{
    std::byte{'R'}, std::byte{'S'}, std::byte{'T'}, std::byte{'T'},
    std::byte{'K'}, std::byte{'T'}, std::byte{'1'}, std::byte{0},
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<std::byte, kKeySize> g_key{};
bool g_ready{};

template <typename Value>
void write_integer(std::span<std::byte, kTicketSize> output,
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
[[nodiscard]] Value read_integer(std::span<const std::byte, kTicketSize> input,
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

[[nodiscard]] bool read_exact(HANDLE file, std::span<std::byte> output) noexcept {
    DWORD read = 0;
    LARGE_INTEGER size{};
    return file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) != FALSE
           && size.QuadPart == static_cast<LONGLONG>(output.size())
           && ReadFile(file,
                       output.data(),
                       static_cast<DWORD>(output.size()),
                       &read,
                       nullptr)
                  != FALSE
           && read == output.size();
}

[[nodiscard]] bool load_existing(const path::Buffer& pathValue,
                                 std::span<std::byte> output) noexcept {
    const HANDLE file = CreateFileW(pathValue.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool success = read_exact(file, output);
    (void)CloseHandle(file);
    return success;
}

[[nodiscard]] bool create_or_load(const path::Buffer& pathValue,
                                  std::span<std::byte> output) noexcept {
    if (load_existing(pathValue, output)) {
        return true;
    }
    const DWORD openError = GetLastError();
    if (openError != ERROR_FILE_NOT_FOUND && openError != ERROR_PATH_NOT_FOUND) {
        return false;
    }
    std::array<std::byte, kKeySize> generated{};
    if (!randomize(generated)) {
        return false;
    }
    const HANDLE file = CreateFileW(pathValue.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const bool raced = GetLastError() == ERROR_FILE_EXISTS && load_existing(pathValue, output);
        SecureZeroMemory(generated.data(), generated.size());
        return raced;
    }
    DWORD written = 0;
    bool success = WriteFile(file,
                             generated.data(),
                             static_cast<DWORD>(generated.size()),
                             &written,
                             nullptr)
                       != FALSE
                   && written == generated.size() && FlushFileBuffers(file) != FALSE;
    success = CloseHandle(file) != FALSE && success;
    if (!success) {
        (void)DeleteFileW(pathValue.chars.data());
    } else {
        std::copy(generated.begin(), generated.end(), output.begin());
    }
    SecureZeroMemory(generated.data(), generated.size());
    return success;
}

[[nodiscard]] bool hmac(std::span<const std::byte> input,
                        std::span<const std::byte> key,
                        std::span<std::byte, kMacSize> output) noexcept {
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())
        || key.size() > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    bool success = BCryptOpenAlgorithmProvider(
                       &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)
                       >= 0
                   && BCryptCreateHash(algorithm,
                                       &hash,
                                       nullptr,
                                       0,
                                       reinterpret_cast<PUCHAR>(
                                           const_cast<std::byte*>(key.data())),
                                       static_cast<ULONG>(key.size()),
                                       0)
                          >= 0
                   && BCryptHashData(hash,
                                     reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
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

[[nodiscard]] bool verify(std::span<const std::byte, kTicketSize> ticket,
                          std::span<const std::byte> key,
                          std::uint64_t nowSeconds,
                          Claims& claims) noexcept {
    if (!std::equal(kMagic.begin(), kMagic.end(), ticket.begin() + kMagicOffset)
        || read_integer<std::uint32_t>(ticket, kVersionOffset) != kFormatVersion) {
        return false;
    }
    std::array<std::byte, kMacSize> expected{};
    if (!hmac(ticket.first<kMacOffset>(), key, expected)) {
        return false;
    }
    unsigned difference = 0;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        difference |= std::to_integer<unsigned>(expected[index] ^ ticket[kMacOffset + index]);
    }
    SecureZeroMemory(expected.data(), expected.size());
    if (difference != 0) {
        return false;
    }
    Claims decoded{};
    decoded.provider = read_integer<std::uint32_t>(ticket, kProviderOffset);
    decoded.platformId = read_integer<std::uint64_t>(ticket, kPlatformIdOffset);
    decoded.issuedAt = read_integer<std::uint64_t>(ticket, kIssuedAtOffset);
    decoded.expiresAt = read_integer<std::uint64_t>(ticket, kExpiresAtOffset);
    if (decoded.provider != kProviderSteam || decoded.platformId == 0
        || decoded.issuedAt > nowSeconds + kFutureSkewSeconds || decoded.expiresAt <= nowSeconds
        || decoded.expiresAt < decoded.issuedAt
        || decoded.expiresAt - decoded.issuedAt != kLifetimeSeconds) {
        return false;
    }
    claims = decoded;
    return true;
}

} // namespace

bool initialize(void* module) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    path::Buffer keyPath;
    const bool success = path::artifact_directory(module, keyPath)
                         && path::append(keyPath, kKeyFileSuffix)
                         && create_or_load(keyPath, g_key);
    g_ready = success;
    if (!success) {
        SecureZeroMemory(g_key.data(), g_key.size());
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool issue(std::uint64_t platformId,
           std::uint64_t nowSeconds,
           std::span<std::byte, kTicketSize> output) noexcept {
    if (platformId == 0
        || nowSeconds > (std::numeric_limits<std::uint64_t>::max)() - kLifetimeSeconds) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    if (!g_ready) {
        ReleaseSRWLockShared(&g_lock);
        return false;
    }
    std::fill(output.begin(), output.end(), std::byte{0});
    std::copy(kMagic.begin(), kMagic.end(), output.begin() + kMagicOffset);
    write_integer(output, kVersionOffset, kFormatVersion);
    write_integer(output, kProviderOffset, kProviderSteam);
    write_integer(output, kPlatformIdOffset, platformId);
    write_integer(output, kIssuedAtOffset, nowSeconds);
    write_integer(output, kExpiresAtOffset, nowSeconds + kLifetimeSeconds);
    bool success = randomize(output.subspan<kNonceOffset, kNonceSize>())
                   && randomize(output.subspan<kRandomPaddingOffset, kRandomPaddingSize>())
                   && hmac(output.first<kMacOffset>(),
                           g_key,
                           output.subspan<kMacOffset, kMacSize>());
    ReleaseSRWLockShared(&g_lock);
    if (!success) {
        SecureZeroMemory(output.data(), output.size());
    }
    return success;
}

bool authenticate(std::span<const std::byte> signOnBody,
                  std::uint64_t nowSeconds,
                  Claims& claims) noexcept {
    claims = {};
    if (signOnBody.size() < kTicketSize) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    if (!g_ready) {
        ReleaseSRWLockShared(&g_lock);
        return false;
    }
    bool authenticated = false;
    for (std::size_t offset = 0; offset <= signOnBody.size() - kTicketSize; ++offset) {
        if (signOnBody[offset] != kMagic.front()) {
            continue;
        }
        const auto ticket = std::span<const std::byte, kTicketSize>(
            signOnBody.data() + offset, kTicketSize);
        if (verify(ticket, g_key, nowSeconds, claims)) {
            authenticated = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return authenticated;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    SecureZeroMemory(g_key.data(), g_key.size());
    g_ready = false;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::core::identity::local_ticket
