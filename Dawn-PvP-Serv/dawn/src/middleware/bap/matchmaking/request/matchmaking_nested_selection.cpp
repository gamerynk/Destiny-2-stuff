#include "../../../protobuf/codec.h"
#include "field_selection.h"

namespace dawn::middleware::bap::matchmaking::request {
namespace {

using protobuf::Field;
using protobuf::Reader;
using protobuf::WireType;

enum class AdvertisementField : std::uint32_t {
    existingId = 1,
    record = 2,
};

enum class RecordField : std::uint32_t {
    descriptor = 2,
    variantKey = 4,
};

constexpr std::uint32_t kDescriptorBytesField = 1;

struct AdvertisementSelection final {
    bool sawExistingId{};
    bool hasExistingId{};
    std::uint64_t existingId{};
    bool sawRecord{};
    bool hasRecordMessage{};
    std::span<const std::byte> recordMessage{};
};

struct RecordSelection final {
    bool sawDescriptor{};
    bool hasDescriptorMessage{};
    std::span<const std::byte> descriptorMessage{};
    bool sawVariantKey{};
    bool hasVariantKey{};
    std::uint64_t variantKey{};
};

struct DescriptorSelection final {
    bool sawBytes{};
    bool hasBytes{};
    std::span<const std::byte> bytes{};
};

[[nodiscard]] bool select_advertisement(std::span<const std::byte> input,
                                        AdvertisementSelection& selected) noexcept {
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field;
        if (!reader.next(field)) {
            return false;
        }
        if (field.fieldNumber == static_cast<std::uint32_t>(AdvertisementField::existingId)
            && !selected.sawExistingId) {
            selected.sawExistingId = true;
            if (field.wireType == WireType::varint) {
                selected.hasExistingId = true;
                selected.existingId = field.value;
            }
        } else if (field.fieldNumber == static_cast<std::uint32_t>(AdvertisementField::record)
                   && !selected.sawRecord) {
            selected.sawRecord = true;
            if (field.wireType == WireType::lengthDelimited) {
                selected.hasRecordMessage = true;
                selected.recordMessage = field.bytes;
            }
        }
    }
    return true;
}

[[nodiscard]] bool select_record(std::span<const std::byte> input,
                                 RecordSelection& selected) noexcept {
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field;
        if (!reader.next(field)) {
            return false;
        }
        // the first occurrence wins even when its wire type is not usable here
        if (field.fieldNumber == static_cast<std::uint32_t>(RecordField::descriptor)
            && !selected.sawDescriptor) {
            selected.sawDescriptor = true;
            if (field.wireType == WireType::lengthDelimited) {
                selected.hasDescriptorMessage = true;
                selected.descriptorMessage = field.bytes;
            }
        } else if (field.fieldNumber == static_cast<std::uint32_t>(RecordField::variantKey)
                   && !selected.sawVariantKey) {
            selected.sawVariantKey = true;
            if (field.wireType == WireType::varint) {
                selected.hasVariantKey = true;
                selected.variantKey = field.value;
            }
        }
    }
    return true;
}

[[nodiscard]] bool select_descriptor(std::span<const std::byte> input,
                                     DescriptorSelection& selected) noexcept {
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field;
        if (!reader.next(field)) {
            return false;
        }
        // a wrong wire type on the first occurrence makes the bytes absent; duplicates are
        // still ignored
        if (field.fieldNumber == kDescriptorBytesField && !selected.sawBytes) {
            selected.sawBytes = true;
            if (field.wireType == WireType::lengthDelimited) {
                selected.hasBytes = true;
                selected.bytes = field.bytes;
            }
        }
    }
    return !selected.hasBytes || selected.bytes.size() == kJoinDescriptorSize;
}

} // namespace

bool parse_update(std::span<const std::byte> input, AdvertisementUpdate& update) noexcept {
    update = {};
    AdvertisementSelection advertisement;
    if (!select_advertisement(input, advertisement)) {
        return false;
    }
    update.hasExistingId = advertisement.hasExistingId;
    update.existingId = advertisement.existingId;
    if (!advertisement.hasRecordMessage) {
        return true;
    }

    RecordSelection record;
    if (!select_record(advertisement.recordMessage, record)) {
        return false;
    }
    if (record.hasVariantKey) {
        update.variantKey = record.variantKey;
    }
    if (!record.hasDescriptorMessage) {
        return true;
    }

    DescriptorSelection descriptor;
    if (!select_descriptor(record.descriptorMessage, descriptor)) {
        return false;
    }
    update.hasDescriptor = descriptor.hasBytes;
    update.descriptor = descriptor.bytes;
    return true;
}

} // namespace dawn::middleware::bap::matchmaking::request
