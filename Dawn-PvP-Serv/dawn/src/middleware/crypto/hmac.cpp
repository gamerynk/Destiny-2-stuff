#include "hmac.h"

#include <Windows.h>

#include <algorithm>
#include <bcrypt.h>
#include <limits>
#include <vector>

#include "murmur3.h"

namespace dawn::middleware::crypto::hmac {

namespace {

constexpr std::size_t kSha1DigestSize = 20;
constexpr std::size_t kPadBlockSize = 64;
constexpr std::byte kInnerPad{0x36};
constexpr std::byte kOuterPad{0x5C};

// hand-built construction the peer uses over a one-shot digest; key must fit one block,
// zero padded and never hashed down
void authenticate_murmur3(std::span<const std::byte> key,
                          std::span<const std::byte> first,
                          std::span<const std::byte> second,
                          Digest& output) noexcept {
    std::array<std::byte, kPadBlockSize> inner{};
    std::array<std::byte, kPadBlockSize> outer{};
    std::copy(key.begin(), key.end(), inner.begin());
    std::copy(key.begin(), key.end(), outer.begin());
    for (std::size_t index = 0; index < kPadBlockSize; ++index) {
        inner[index] ^= kInnerPad;
        outer[index] ^= kOuterPad;
    }

    std::vector<std::byte> body;
    body.reserve(inner.size() + first.size() + second.size());
    body.insert(body.end(), inner.begin(), inner.end());
    body.insert(body.end(), first.begin(), first.end());
    body.insert(body.end(), second.begin(), second.end());

    murmur3::Digest digest{};
    murmur3::hash(body, {}, digest);
    murmur3::Digest sealed{};
    murmur3::hash(outer, digest, sealed);

    output.size = murmur3::kDigestSize;
    std::copy(sealed.begin(), sealed.end(), output.bytes.begin());
    // the pads and the body carry the key
    SecureZeroMemory(inner.data(), inner.size());
    SecureZeroMemory(outer.data(), outer.size());
    SecureZeroMemory(body.data(), body.size());
}

[[nodiscard]] bool succeeded(NTSTATUS status) noexcept {
    return status >= 0;
}

[[nodiscard]] LPCWSTR provider(Algorithm algorithm) noexcept {
    return algorithm == Algorithm::sha1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA256_ALGORITHM;
}

[[nodiscard]] std::size_t digest_size(Algorithm algorithm) noexcept {
    return algorithm == Algorithm::sha1 ? kSha1DigestSize : kMaximumDigestSize;
}

[[nodiscard]] bool add(BCRYPT_HASH_HANDLE handle, std::span<const std::byte> part) noexcept {
    if (part.empty()) {
        return true;
    }
    if (part.size() > (std::numeric_limits<ULONG>::max)()) {
        return false;
    }
    return succeeded(BCryptHashData(handle,
                                    reinterpret_cast<PUCHAR>(const_cast<std::byte*>(part.data())),
                                    static_cast<ULONG>(part.size()),
                                    0));
}

} // namespace

bool authenticate(Algorithm algorithm,
                  std::span<const std::byte> key,
                  std::span<const std::byte> first,
                  std::span<const std::byte> second,
                  Digest& output) noexcept {
    if (algorithm == Algorithm::murmur3) {
        if (key.size() > kPadBlockSize) {
            return false;
        }
        authenticate_murmur3(key, first, second, output);
        return true;
    }
    BCRYPT_ALG_HANDLE opened = nullptr;
    if (!succeeded(BCryptOpenAlgorithmProvider(
            &opened, provider(algorithm), nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return false;
    }
    output.size = digest_size(algorithm);
    BCRYPT_HASH_HANDLE handle = nullptr;
    bool complete = false;
    if (succeeded(BCryptCreateHash(opened,
                                   &handle,
                                   nullptr,
                                   0,
                                   reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
                                   static_cast<ULONG>(key.size()),
                                   0))) {
        complete = add(handle, first) && add(handle, second)
                   && succeeded(BCryptFinishHash(handle,
                                                 reinterpret_cast<PUCHAR>(output.bytes.data()),
                                                 static_cast<ULONG>(output.size),
                                                 0));
        BCryptDestroyHash(handle);
    }
    BCryptCloseAlgorithmProvider(opened, 0);
    if (!complete) {
        output = {};
    }
    return complete;
}

} // namespace dawn::middleware::crypto::hmac
