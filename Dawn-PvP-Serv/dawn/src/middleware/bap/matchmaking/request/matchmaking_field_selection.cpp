#include "../../../protobuf/codec.h"
#include "field_selection.h"

namespace dawn::middleware::bap::matchmaking::request {
namespace {

using protobuf::Field;
using protobuf::Reader;
using protobuf::WireType;

enum class RootField : std::uint32_t {
    kind = 2,
    requestNonce = 3,
    advertisement = 8,
};

struct RawRoot final {
    bool sawKind{};
    bool hasKindValue{};
    std::uint64_t kindValue{};
    bool sawRequestNonce{};
    bool hasRequestNonce{};
    std::uint64_t requestNonce{};
    bool sawAdvertisement{};
    bool hasAdvertisementMessage{};
    std::span<const std::byte> advertisementMessage{};
};

[[nodiscard]] RequestKind request_kind(std::uint64_t value) noexcept {
    switch (value) {
    case static_cast<std::uint64_t>(RequestKind::sessionSearch):
        return RequestKind::sessionSearch;
    case static_cast<std::uint64_t>(RequestKind::advertisementUpdate):
        return RequestKind::advertisementUpdate;
    case static_cast<std::uint64_t>(RequestKind::advertisementDelete):
        return RequestKind::advertisementDelete;
    case static_cast<std::uint64_t>(RequestKind::configuration):
        return RequestKind::configuration;
    case static_cast<std::uint64_t>(RequestKind::rejoinAdvertisementUpdate):
        return RequestKind::rejoinAdvertisementUpdate;
    case static_cast<std::uint64_t>(RequestKind::rejoinAdvertisementDelete):
        return RequestKind::rejoinAdvertisementDelete;
    case static_cast<std::uint64_t>(RequestKind::locateSession):
        return RequestKind::locateSession;
    case static_cast<std::uint64_t>(RequestKind::liveStats):
        return RequestKind::liveStats;
    default:
        return RequestKind::none;
    }
}

[[nodiscard]] bool select_root(std::span<const std::byte> input, RawRoot& selected) noexcept {
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field;
        if (!reader.next(field)) {
            return false;
        }
        // mark the field before checking its wire type, so a later duplicate can't replace
        // the first
        if (field.fieldNumber == static_cast<std::uint32_t>(RootField::kind) && !selected.sawKind) {
            selected.sawKind = true;
            if (field.wireType == WireType::varint) {
                selected.hasKindValue = true;
                selected.kindValue = field.value;
            }
        } else if (field.fieldNumber
                       == static_cast<std::uint32_t>(RootField::requestNonce)
                   && !selected.sawRequestNonce) {
            selected.sawRequestNonce = true;
            if (field.wireType == WireType::varint) {
                selected.hasRequestNonce = true;
                selected.requestNonce = field.value;
            }
        } else if (field.fieldNumber == static_cast<std::uint32_t>(RootField::advertisement)
                   && !selected.sawAdvertisement) {
            selected.sawAdvertisement = true;
            if (field.wireType == WireType::lengthDelimited) {
                selected.hasAdvertisementMessage = true;
                selected.advertisementMessage = field.bytes;
            }
        }
    }
    return true;
}

} // namespace

bool parse_root(std::span<const std::byte> input, Root& selected) noexcept {
    selected = {};
    RawRoot raw;
    if (!select_root(input, raw)) {
        return false;
    }
    if (raw.hasKindValue) {
        selected.kind = request_kind(raw.kindValue);
    }
    selected.hasRequestNonce = raw.hasRequestNonce;
    selected.requestNonce = raw.requestNonce;
    selected.hasAdvertisementMessage = raw.hasAdvertisementMessage;
    selected.advertisementMessage = raw.advertisementMessage;
    return true;
}

} // namespace dawn::middleware::bap::matchmaking::request
