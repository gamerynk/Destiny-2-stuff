#include "ecc_p224.h"

#include <Windows.h>

#include <algorithm>
#include <vector>

#include "ecc_p224_curve.h"
#include "random_bytes.h"

namespace dawn::middleware::crypto::ecc {

namespace {

constexpr std::byte kTagSequence{0x30};
constexpr std::byte kTagInteger{0x02};
constexpr std::byte kTagBitString{0x03};
// key encoding opens with a one-bit flag that is set only on a private key
constexpr std::array<std::byte, 4> kPublicFlagBits{
    kTagBitString, std::byte{0x02}, std::byte{0x07}, std::byte{0x00}};
constexpr std::size_t kShortFormLimit = 0x80;
// a draw outside 1..n-1 is rarer than 1 in 2^112, so hitting the limit means broken system randomness
constexpr unsigned kScalarAttempts = 4;

void append_integer(std::vector<std::byte>& output, std::span<const std::byte> value) noexcept {
    std::size_t first = 0;
    while (first + 1 < value.size() && value[first] == std::byte{0}) {
        ++first;
    }
    const std::span<const std::byte> trimmed = value.subspan(first);
    const bool pad = (std::to_integer<unsigned>(trimmed[0]) & 0x80U) != 0;
    output.push_back(kTagInteger);
    output.push_back(static_cast<std::byte>(trimmed.size() + (pad ? 1U : 0U)));
    if (pad) {
        output.push_back(std::byte{0});
    }
    output.insert(output.end(), trimmed.begin(), trimmed.end());
}

// encode the way the peer's importer reads it, zero padded to the fixed field
[[nodiscard]] bool encode_public_key(std::span<const std::byte> x,
                                     std::span<const std::byte> y,
                                     std::array<std::byte, kExportedKeySize>& output) noexcept {
    std::vector<std::byte> body;
    body.insert(body.end(), kPublicFlagBits.begin(), kPublicFlagBits.end());
    // curve is identified by its field size, as a plain integer
    body.push_back(kTagInteger);
    body.push_back(std::byte{1});
    body.push_back(static_cast<std::byte>(kFieldSize));
    append_integer(body, x);
    append_integer(body, y);
    if (body.size() >= kShortFormLimit || body.size() + 2 > kExportedKeySize) {
        return false;
    }
    output = {};
    output[0] = kTagSequence;
    output[1] = static_cast<std::byte>(body.size());
    std::copy(body.begin(), body.end(), output.begin() + 2);
    return true;
}

[[nodiscard]] bool read_header(std::span<const std::byte> input,
                               std::size_t& cursor,
                               std::byte tag,
                               std::size_t& length) noexcept {
    if (cursor + 2 > input.size() || input[cursor] != tag) {
        return false;
    }
    const auto declared = std::to_integer<std::size_t>(input[cursor + 1]);
    // short form only; every element here is far below the long-form threshold
    if (declared >= kShortFormLimit || cursor + 2 + declared > input.size()) {
        return false;
    }
    cursor += 2;
    length = declared;
    return true;
}

[[nodiscard]] bool read_field_integer(std::span<const std::byte> input,
                                      std::size_t& cursor,
                                      std::array<std::byte, kFieldSize>& output) noexcept {
    std::size_t length = 0;
    if (!read_header(input, cursor, kTagInteger, length) || length == 0) {
        return false;
    }
    std::span<const std::byte> value = input.subspan(cursor, length);
    if (!value.empty() && value[0] == std::byte{0}) {
        value = value.subspan(1);
    }
    if (value.size() > kFieldSize) {
        return false;
    }
    output = {};
    std::copy(value.begin(), value.end(), output.end() - static_cast<std::ptrdiff_t>(value.size()));
    cursor += length;
    return true;
}

[[nodiscard]] bool decode_public_key(std::span<const std::byte> input,
                                     std::array<std::byte, kFieldSize>& x,
                                     std::array<std::byte, kFieldSize>& y) noexcept {
    std::size_t cursor = 0;
    std::size_t length = 0;
    if (!read_header(input, cursor, kTagSequence, length)) {
        return false;
    }
    if (!read_header(input, cursor, kTagBitString, length)) {
        return false;
    }
    cursor += length;
    std::size_t sizeLength = 0;
    if (!read_header(input, cursor, kTagInteger, sizeLength) || sizeLength != 1
        || input[cursor] != static_cast<std::byte>(kFieldSize)) {
        return false;
    }
    cursor += sizeLength;
    return read_field_integer(input, cursor, x) && read_field_integer(input, cursor, y);
}

[[nodiscard]] bool generate_scalar(curve::Field& output) noexcept {
    std::array<std::byte, kFieldSize> drawn{};
    for (unsigned attempt = 0; attempt < kScalarAttempts; ++attempt) {
        if (!random::fill(drawn)) {
            break;
        }
        curve::load(drawn, output);
        SecureZeroMemory(drawn.data(), drawn.size());
        if (curve::valid_scalar(output)) {
            return true;
        }
    }
    SecureZeroMemory(drawn.data(), drawn.size());
    output = {};
    return false;
}

} // namespace

bool agree(std::span<const std::byte> peerPublicKey, Agreement& output) noexcept {
    std::array<std::byte, kFieldSize> peerX{};
    std::array<std::byte, kFieldSize> peerY{};
    if (!decode_public_key(peerPublicKey, peerX, peerY)) {
        return false;
    }
    curve::Point peer{};
    curve::load(peerX, peer.x);
    curve::load(peerY, peer.y);
    if (!curve::on_curve(peer)) {
        return false;
    }

    curve::Field secret{};
    if (!generate_scalar(secret)) {
        return false;
    }
    curve::Point ours{};
    curve::Point agreed{};
    const bool multiplied =
        curve::multiply(secret, curve::generator(), ours) && curve::multiply(secret, peer, agreed);
    SecureZeroMemory(secret.data(), secret.size() * sizeof(secret[0]));
    if (!multiplied) {
        return false;
    }

    std::array<std::byte, kFieldSize> ourX{};
    std::array<std::byte, kFieldSize> ourY{};
    curve::store(ours.x, ourX);
    curve::store(ours.y, ourY);
    curve::store(agreed.x, output.sharedSecret);
    // agreed point is secret; only the stored copy may outlive this call
    SecureZeroMemory(&agreed, sizeof(agreed));
    if (!encode_public_key(ourX, ourY, output.publicKey)) {
        SecureZeroMemory(output.sharedSecret.data(), output.sharedSecret.size());
        output = {};
        return false;
    }
    return true;
}

} // namespace dawn::middleware::crypto::ecc
