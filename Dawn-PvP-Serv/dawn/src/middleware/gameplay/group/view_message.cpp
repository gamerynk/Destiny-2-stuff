#include "view_message.h"

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::group {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kKindWidth = 3;
constexpr std::uint8_t kFlagWidth = 1;
constexpr std::uint8_t kOptionalWidth = 32;
constexpr std::uint8_t kListCountWidth = 7;
constexpr std::uint8_t kTokenWidth = 64;

} // namespace

bool read_view(bits::Reader& reader, ViewEstablishment& output) noexcept {
    ViewEstablishment candidate{};
    std::uint64_t kind = 0;
    std::uint64_t present = 0;
    if (!reader.read(kKindWidth, kind) || kind > kMaximumViewKind
        || !reader.read(kFlagWidth, present)) {
        return false;
    }
    candidate.kind = static_cast<std::uint8_t>(kind);
    candidate.hasOptionalValue = present != 0;
    if (candidate.hasOptionalValue) {
        std::uint64_t value = 0;
        if (!reader.read(kOptionalWidth, value)) {
            return false;
        }
        candidate.optionalValue = static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
        if (candidate.optionalValue < -1) return false;
    }
    std::uint64_t hasList = 0;
    if (!reader.read(kFlagWidth, hasList)) {
        return false;
    }
    candidate.hasList = hasList != 0;
    if (candidate.hasList) {
        std::uint64_t count = 0;
        if (!reader.read(kListCountWidth, count) || count > kViewListCapacity) {
            return false;
        }
        candidate.listCount = static_cast<std::uint8_t>(count);
        if (!bits::read_raw(reader, {candidate.list.data(), candidate.listCount})) {
            return false;
        }
    }
    if (!reader.read(kTokenWidth, candidate.sessionToken)) {
        return false;
    }
    output = candidate;
    return true;
}

bool write_view(bits::Writer& writer, const ViewEstablishment& body) noexcept {
    if (body.kind > kMaximumViewKind || body.listCount > kViewListCapacity
        || (body.hasOptionalValue && body.optionalValue < -1)) {
        return false;
    }
    const bool revisionPresent = body.hasOptionalValue && body.optionalValue != -1;
    if (!writer.write(body.kind, kKindWidth)
        || !writer.write(revisionPresent ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (revisionPresent
        && !writer.write(static_cast<std::uint32_t>(body.optionalValue), kOptionalWidth)) {
        return false;
    }
    if (!writer.write(body.hasList ? 1U : 0U, kFlagWidth)) {
        return false;
    }
    if (body.hasList) {
        if (!writer.write(body.listCount, kListCountWidth)
            || !bits::write_raw(writer, {body.list.data(), body.listCount})) {
            return false;
        }
    }
    return writer.write(body.sessionToken, kTokenWidth);
}

bool compatible(const ViewEstablishment& left, const ViewEstablishment& right) noexcept {
    if (left.sessionToken != right.sessionToken || left.kind != right.kind
        || left.hasList != right.hasList || left.listCount != right.listCount) {
        return false;
    }
    for (std::size_t index = 0; index < left.listCount; ++index) {
        if (left.list[index] != right.list[index]) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::middleware::gameplay::group
