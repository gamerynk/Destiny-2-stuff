#include <array>
#include <cstdint>

#include "../../protobuf/codec.h"
#include "encoder.h"

namespace dawn::middleware::content::manifest {
namespace {

constexpr std::uint32_t kHandleField = 1;
constexpr std::uint32_t kReservedField = 2;
constexpr std::uint32_t kNameWrapperField = 3;
// MsgB field 4 picks the entitlement kind; installed content uses kind 2
constexpr std::uint32_t kKindField = 4;
constexpr std::uint32_t kKindReservedField = 5;
constexpr std::uint32_t kTrailingReservedField = 7;
constexpr std::uint32_t kNameInnerField = 4;
constexpr std::uint32_t kNameField = 1;
constexpr std::uint64_t kInstalledContentKind = 2;
constexpr std::uint64_t kReservedValue = 0;
constexpr std::size_t kNameWrapperCapacity = state::entitlements::kNameCapacity + 8;

} // namespace

bool encode_entitlement(const state::entitlements::Entitlement& entry,
                        std::uint32_t definitionHandle,
                        std::span<std::byte> output,
                        std::size_t& size) noexcept {
    size = 0;
    const auto name = std::as_bytes(std::span(entry.name.data(), entry.nameLength));
    std::array<std::byte, kNameWrapperCapacity> inner{};
    protobuf::Writer innerWriter(inner);
    if (!innerWriter.write_length_delimited(kNameField, name)) {
        return false;
    }
    std::array<std::byte, kNameWrapperCapacity> wrapper{};
    protobuf::Writer wrapperWriter(wrapper);
    if (!wrapperWriter.write_length_delimited(kNameInnerField,
                                              std::span(inner).first(innerWriter.size()))) {
        return false;
    }

    protobuf::Writer writer(output);
    if (!writer.write_varint(kHandleField, definitionHandle)
        || !writer.write_varint(kReservedField, kReservedValue)
        || !writer.write_length_delimited(kNameWrapperField,
                                          std::span(wrapper).first(wrapperWriter.size()))
        || !writer.write_varint(kKindField, kInstalledContentKind)
        || !writer.write_varint(kKindReservedField, kReservedValue)
        || !writer.write_varint(kTrailingReservedField, kReservedValue)) {
        return false;
    }
    size = writer.size();
    return true;
}

} // namespace dawn::middleware::content::manifest
