#include "protected_datagram.h"

#include "../../crypto/aes_gcm_decrypt.h"
#include "../../crypto/aes_gcm_encrypt.h"
#include "../../encoding/bit_raw.h"
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace dawn::middleware::gameplay::association {

namespace {

namespace bits = encoding::bits;

constexpr std::uint64_t kEstablishedMarker = 0;
constexpr std::uint8_t kMarkerWidth = 1;
constexpr std::uint8_t kWordWidth = 32;
constexpr std::uint8_t kModeWidth = 3;
constexpr std::uint8_t kLengthWidth = 11;
// only mode zero is produced or accepted; legacy modes are not negotiated
constexpr std::uint64_t kMode = 0;
// leading bit of the plaintext, meaning unverified; we send 0 and accept either
constexpr std::uint64_t kInnerMarker = 0;
constexpr std::uint8_t kInnerMarkerWidth = 1;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFF;
// the 90-bit clear header padded to a byte boundary
constexpr std::size_t kHeaderSize = 12;

void derive_nonce(std::span<const std::byte, kNonceSize> base,
                  std::uint32_t wordA,
                  std::uint32_t wordB,
                  std::span<std::byte, kNonceSize> output) noexcept {
    // each clear word covers four bytes of the nonce base, big-endian
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    for (std::size_t index = 0; index < kNonceSize; ++index) {
        output[index] = base[index];
    }
    for (std::size_t index = 0; index < kWordBytes; ++index) {
        const unsigned shift = static_cast<unsigned>(kWordBytes - 1 - index) * kByteBits;
        const auto highByte = static_cast<std::byte>((wordA >> shift) & kByteMask);
        const auto lowByte = static_cast<std::byte>((wordB >> shift) & kByteMask);
        output[index] ^= highByte;
        output[kWordBytes + index] ^= lowByte;
    }
}

} // namespace

bool seal(ProtectedContext& context,
          std::span<const std::byte> payload,
          std::uint16_t destinationPort,
          std::span<std::byte> output,
          std::size_t& written) noexcept {
    written = 0;
    if (!context.installed || payload.size() > kMaximumPayload
        || output.size() < kProtectedCapacity) {
        return false;
    }
    // word A advances before the header so no two sends share a nonce
    ++context.outboundWordA;

    std::array<std::byte, kProtectedCapacity> plaintext{};
    bits::Writer plaintextWriter(plaintext);
    std::array<std::byte, kTrailerSize> trailer{
        static_cast<std::byte>(destinationPort & kByteMask),
        static_cast<std::byte>((destinationPort >> kByteBits) & kByteMask),
    };
    std::size_t plaintextSize = 0;
    if (!plaintextWriter.write(kInnerMarker, kInnerMarkerWidth)
        || !bits::write_raw(plaintextWriter, payload) || !bits::write_raw(plaintextWriter, trailer)
        || !plaintextWriter.finish(plaintextSize)) {
        return false;
    }

    std::array<std::byte, kHeaderSize> header{};
    bits::Writer headerWriter(header);
    std::size_t headerSize = 0;
    if (!headerWriter.write(kEstablishedMarker, kMarkerWidth)
        || !headerWriter.write(context.outboundWordA, kWordWidth)
        || !headerWriter.write(context.outboundWordB, kWordWidth)
        || !headerWriter.write(kMode, kModeWidth)
        || !headerWriter.write(payload.size(), kLengthWidth)
        || !headerWriter.write(trailer.size(), kLengthWidth) || !headerWriter.finish(headerSize)
        || headerSize != kHeaderSize) {
        return false;
    }

    std::array<std::byte, kNonceSize> nonce{};
    derive_nonce(context.outboundBase, context.outboundWordA, context.outboundWordB, nonce);
    std::array<std::byte, kTagSize> tag{};
    const std::size_t total = kHeaderSize + plaintextSize + kTagSize;
    if (total > output.size()
        || !crypto::aes_gcm::encrypt(context.key,
                                     nonce,
                                     std::span<const std::byte>(plaintext.data(), plaintextSize),
                                     output.subspan(kHeaderSize, plaintextSize),
                                     tag)) {
        return false;
    }
    for (std::size_t index = 0; index < kHeaderSize; ++index) {
        output[index] = header[index];
    }
    for (std::size_t index = 0; index < kTagSize; ++index) {
        output[kHeaderSize + plaintextSize + index] = tag[index];
    }
    written = total;
    return true;
}

bool open(const ProtectedContext& context,
          std::span<const std::byte> datagram,
          std::span<std::byte> payload,
          std::size_t& payloadSize,
          std::uint32_t& wordA) noexcept {
    payloadSize = 0;
    if (!context.installed || datagram.size() <= kHeaderSize + kTagSize
        || datagram.size() > kProtectedCapacity) {
        return false;
    }
    bits::Reader headerReader(datagram.subspan(0, kHeaderSize));
    std::uint64_t marker = 0;
    std::uint64_t receivedWordA = 0;
    std::uint64_t receivedWordB = 0;
    std::uint64_t mode = 0;
    std::uint64_t declaredPayload = 0;
    std::uint64_t declaredTrailer = 0;
    if (!headerReader.read(kMarkerWidth, marker) || marker != kEstablishedMarker
        || !headerReader.read(kWordWidth, receivedWordA)
        || !headerReader.read(kWordWidth, receivedWordB) || !headerReader.read(kModeWidth, mode)
        || mode != kMode || !headerReader.read(kLengthWidth, declaredPayload)
        || !headerReader.read(kLengthWidth, declaredTrailer)) {
        return false;
    }

    const std::size_t cipherSize = datagram.size() - kHeaderSize - kTagSize;
    std::array<std::byte, kNonceSize> nonce{};
    derive_nonce(context.inboundBase,
                 static_cast<std::uint32_t>(receivedWordA),
                 static_cast<std::uint32_t>(receivedWordB),
                 nonce);
    std::array<std::byte, kProtectedCapacity> plaintext{};
    if (!crypto::aes_gcm::decrypt(
            context.key,
            nonce,
            datagram.subspan(kHeaderSize, cipherSize),
            datagram.subspan(kHeaderSize + cipherSize, kTagSize).first<kTagSize>(),
            plaintext)) {
        return false;
    }

    bits::Reader plaintextReader(std::span<const std::byte>(plaintext.data(), cipherSize));
    std::uint64_t innerMarker = 0;
    if (!plaintextReader.read(kInnerMarkerWidth, innerMarker)) {
        return false;
    }
    const auto declared = static_cast<std::size_t>(declaredPayload);
    if (declared > payload.size() || !bits::read_raw(plaintextReader, payload.subspan(0, declared))
        || !bits::skip_raw(plaintextReader, static_cast<std::size_t>(declaredTrailer))) {
        return false;
    }
    payloadSize = declared;
    wordA = static_cast<std::uint32_t>(receivedWordA);
    return true;
}

} // namespace dawn::middleware::gameplay::association
