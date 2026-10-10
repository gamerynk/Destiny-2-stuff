#include "association_keys.h"

#include <Windows.h>

#include <algorithm>
#include <string_view>

#include "../../crypto/tiger192.h"

namespace dawn::middleware::gameplay::dtls {

namespace {

constexpr std::size_t kSecretOffset = 0;
constexpr std::size_t kSecretSize = 28;
constexpr std::size_t kSecurityKeyOffset = kSecretOffset + kSecretSize;
constexpr std::size_t kSuffixOffset = kSecurityKeyOffset + kSecurityKeySize;
// cypher suffix travels without a terminator
constexpr std::string_view kCypherSuffix{"cypherKeySuffix"};
// auth suffix includes its terminator in the buffer
constexpr std::string_view kAuthSuffix{"SuffixForHMACKey"};

void build(std::span<const std::byte> sharedSecret,
           std::span<const std::byte> securityKey,
           std::string_view suffix,
           bool terminated,
           std::array<std::byte, kDerivationSize>& output) noexcept {
    output = {};
    std::copy(sharedSecret.begin(), sharedSecret.end(), output.begin() + kSecretOffset);
    std::copy(securityKey.begin(), securityKey.end(), output.begin() + kSecurityKeyOffset);
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        output[kSuffixOffset + index] = static_cast<std::byte>(suffix[index]);
    }
    if (terminated) {
        output[kSuffixOffset + suffix.size()] = std::byte{0};
    }
}

} // namespace

bool derive(std::span<const std::byte> sharedSecret,
            std::span<const std::byte> securityKey,
            Keys& output) noexcept {
    if (sharedSecret.size() != kSecretSize || securityKey.size() != kSecurityKeySize) {
        return false;
    }
    std::array<std::byte, kDerivationSize> buffer{};
    crypto::tiger::Digest digest{};

    build(sharedSecret, securityKey, kCypherSuffix, false, buffer);
    crypto::tiger::hash(buffer, digest);
    std::copy_n(digest.begin(), kCypherKeySize, output.cypher.begin());

    build(sharedSecret, securityKey, kAuthSuffix, true, buffer);
    crypto::tiger::hash(buffer, digest);
    std::copy_n(digest.begin(), kAuthKeySize, output.auth.begin());
    // the buffer holds the secret and join key; the digest holds both derived keys
    SecureZeroMemory(buffer.data(), buffer.size());
    SecureZeroMemory(digest.data(), digest.size());
    return true;
}

} // namespace dawn::middleware::gameplay::dtls
