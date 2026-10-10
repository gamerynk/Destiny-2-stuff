#include "sha256.h"

#include <Windows.h>

#include <bcrypt.h>
#include <limits>

namespace dawn::middleware::crypto::sha256 {

namespace {

[[nodiscard]] bool succeeded(NTSTATUS status) noexcept {
    return status >= 0;
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

[[nodiscard]] bool digest(std::span<const std::byte> first,
                          std::span<const std::byte> second,
                          Digest& output) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!succeeded(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return false;
    }
    BCRYPT_HASH_HANDLE handle = nullptr;
    bool complete = false;
    if (succeeded(BCryptCreateHash(algorithm, &handle, nullptr, 0, nullptr, 0, 0))) {
        complete = add(handle, first) && add(handle, second)
                   && succeeded(BCryptFinishHash(handle,
                                                 reinterpret_cast<PUCHAR>(output.data()),
                                                 static_cast<ULONG>(output.size()),
                                                 0));
        BCryptDestroyHash(handle);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return complete;
}

} // namespace

bool hash(std::span<const std::byte> input, Digest& output) noexcept {
    return digest(input, {}, output);
}

bool hash_pair(std::span<const std::byte> first,
               std::span<const std::byte> second,
               Digest& output) noexcept {
    return digest(first, second, output);
}

} // namespace dawn::middleware::crypto::sha256
