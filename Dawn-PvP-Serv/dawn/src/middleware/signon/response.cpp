#include "response.h"

#include <Windows.h>

#include <array>
#include <span>
#include <string_view>

#include "config/signon_config_blob.h"
#include "extended/signon_extended_fields.h"
#include "internal.h"
#include "ownership/signon_ownership_encoder.h"

namespace dawn::middleware::signon {
namespace {

constexpr std::size_t kSuccessBufferSize = 320;
constexpr std::size_t kReservedSize = 16;
constexpr unsigned kReservedField = 1;
constexpr unsigned kEncryptionKeyField = 2;
constexpr unsigned kAuthenticationKeyField = 3;
constexpr unsigned kSessionTokenField = 4;
constexpr unsigned kExpiryField = 5;
constexpr unsigned kRelayAddressField = 6;
constexpr unsigned kRelayPortField = 7;
constexpr unsigned kResponseErrorField = 1;
constexpr unsigned kResponseSuccessField = 2;
constexpr unsigned kCommonInfoField = 4;
constexpr unsigned kSiloIdField = 5;
constexpr unsigned kContentBuildVersionField = 6;
constexpr unsigned kCapabilityFlagsField = 8;
constexpr unsigned kOwnedEntitlementsField = 10;
constexpr unsigned kServerTimeField = 12;
constexpr unsigned kEnvironmentField = 13;
constexpr unsigned kExternalAddressField = 14;
constexpr unsigned kCommonInfoIdField = 1;
constexpr unsigned kCommonInfoSiloField = 2;
constexpr unsigned kCommonInfoConfigBlobField = 3;
constexpr std::uint64_t kNoError = 0;
// mock SignOn identity the client accepts; not secret, not derived from the package
constexpr std::uint64_t kCommonId = 1;
constexpr std::uint32_t kSiloId = 1;
constexpr std::uint32_t kCapabilityFlags = 0;
constexpr std::string_view kEnvironment = "live";
constexpr std::string_view kContentBuildVersion = "d2legacy";
constexpr std::size_t kCommonInfoBufferSize = config::kBlobCapacity + 16;

} // namespace

bool encode_success(const state::SignOnState& state,
                    const state::entitlements::Table& entitlements,
                    std::uint64_t expirySeconds,
                    std::uint64_t serverTimeSeconds,
                    std::span<std::byte> output,
                    std::size_t& written,
                    ContentConfiguration content) noexcept {
    written = 0;
    std::array<std::byte, kSuccessBufferSize> successBuffer{};
    Writer success(successBuffer);
    constexpr std::array<std::byte, kReservedSize> reserved{};
    const bool successEncoded = success.bytes(kReservedField, reserved)
                                && success.bytes(kEncryptionKeyField, state.encryptionKey)
                                && success.bytes(kAuthenticationKeyField, state.authenticationKey)
                                && success.bytes(kSessionTokenField, state.sessionToken)
                                && success.varint(kExpiryField, expirySeconds)
                                && success.varint(kRelayAddressField, state.relayAddress)
                                && success.varint(kRelayPortField, state.relayPort)
                                && extended::append(success, state.relayAddress);
    if (!successEncoded) {
        SecureZeroMemory(successBuffer.data(), successBuffer.size());
        return false;
    }

    std::array<std::byte, kCommonInfoBufferSize> commonInfoBuffer{};
    Writer commonInfo(commonInfoBuffer);
    std::array<std::byte, config::kBlobCapacity> configBlob{};
    // without the token the client can't derive its content id and package validation fails
    const bool blobReady = state.bootstrapTokenPresent;
    std::size_t configSize = config::kBlobSize;
    const bool contentRequested = !content.url.empty() || !content.guid.empty();
    const bool blobEncoded = !blobReady || (contentRequested
        ? config::build(state.bootstrapToken, content.url, content.guid, configBlob, configSize)
        : config::build(state.bootstrapToken, std::span(configBlob).first(config::kBlobSize)));
    const bool commonInfoEncoded =
        blobEncoded && commonInfo.varint(kCommonInfoIdField, kCommonId)
        && commonInfo.varint(kCommonInfoSiloField, kSiloId)
        && (!blobReady || commonInfo.bytes(kCommonInfoConfigBlobField, std::span(configBlob).first(configSize)));

    std::array<std::byte, ownership::kBufferSize> ownedBuffer{};
    std::size_t ownedSize = 0;
    const bool ownedEncoded = ownership::encode(entitlements, ownedBuffer, ownedSize);

    Writer response(output);
    const bool responseEncoded =
        commonInfoEncoded && ownedEncoded && response.varint(kResponseErrorField, kNoError)
        && response.bytes(kResponseSuccessField, std::span(successBuffer).first(success.size()))
        && response.bytes(kCommonInfoField, std::span(commonInfoBuffer).first(commonInfo.size()))
        && response.varint(kSiloIdField, kSiloId)
        && response.bytes(kContentBuildVersionField, text_bytes(kContentBuildVersion))
        && response.varint(kCapabilityFlagsField, kCapabilityFlags)
        && response.bytes(kOwnedEntitlementsField, std::span(ownedBuffer).first(ownedSize))
        && response.varint(kServerTimeField, serverTimeSeconds)
        && response.bytes(kEnvironmentField, text_bytes(kEnvironment))
        && response.varint(kExternalAddressField, state.relayAddress);
    SecureZeroMemory(successBuffer.data(), successBuffer.size());
    SecureZeroMemory(commonInfoBuffer.data(), commonInfoBuffer.size());
    SecureZeroMemory(configBlob.data(), configBlob.size());
    if (!responseEncoded) {
        written = 0;
        return false;
    }
    written = response.size();
    return true;
}

} // namespace dawn::middleware::signon
