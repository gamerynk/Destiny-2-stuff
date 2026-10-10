#include "activity_manager_selection_parser.h"

#include "../../../../protobuf/codec.h"
#include "internal.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {
namespace {

using protobuf::Field;
using protobuf::Reader;
using protobuf::WireType;

constexpr std::uint32_t kSelectionFieldNumber = 1;
constexpr std::uint32_t kSnapshotFieldNumber = 2;

struct SelectedField final {
    bool seen{};
    std::span<const std::byte> bytes{};
};

// best effort: a field that won't decode keeps what was found, since dropping it would
// replace the client's destination with an authored one
void select_fields(std::span<const std::byte> input,
                   SelectedField& request,
                   SelectedField& snapshot) noexcept {
    request = {};
    snapshot = {};
    Reader reader(input);
    while (reader.remaining() != 0) {
        Field field{};
        if (!reader.next(field)) {
            return;
        }
        if (field.wireType != WireType::lengthDelimited) {
            continue;
        }
        SelectedField* target = field.fieldNumber == kSelectionFieldNumber  ? &request
                                : field.fieldNumber == kSnapshotFieldNumber ? &snapshot
                                                                            : nullptr;
        if (target == nullptr || target->seen) {
            continue;
        }
        target->seen = true;
        target->bytes = field.bytes;
    }
}

} // namespace

bool parse_selection(std::span<const std::byte> input,
                     ActivityManagerSelectionResult& result) noexcept {
    result = {};
    SelectedField request{};
    SelectedField snapshot{};
    select_fields(input, request, snapshot);

    ActivityManagerSelectionResult committed{};
    if (request.seen && !request.bytes.empty()) {
        committed.hasSelection = parse_field_one(request.bytes, committed.selection);
    }
    if (snapshot.seen && !snapshot.bytes.empty()) {
        committed.hasSecondary = parse_field_two(snapshot.bytes, committed.secondary);
    }
    result = committed;
    // a missing or empty carrier means no selection; only report a carrier that failed to
    // decode
    return committed.hasSelection || committed.hasSecondary
           || !(request.seen && !request.bytes.empty());
}

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
