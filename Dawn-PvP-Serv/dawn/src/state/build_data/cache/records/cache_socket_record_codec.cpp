#include "codec.h"

namespace dawn::state::build_data::cache::records {
namespace {

constexpr unsigned int kReservedFieldValue = 0;

} // namespace

bool encode(const socket_entry_lists::Definition& value, SocketEntryListRecord& record) noexcept {
    record = {
        value.definitionHash,
        value.definitionIndex,
        value.entryCount,
        kReservedFieldValue,
        value.readyMask,
    };
    return true;
}

bool decode(const SocketEntryListRecord& record, socket_entry_lists::Definition& value) noexcept {
    value = {};
    if (record.reserved != kReservedFieldValue) {
        return false;
    }
    value = {
        record.definitionHash,
        record.definitionIndex,
        record.entryCount,
        record.readyMask,
    };
    return true;
}

bool encode(const socket_entry_lists::EntryTable& value, SocketEntryTableRecord& record) noexcept {
    record = {};
    record.definitionIndex = value.definitionIndex;
    for (std::size_t index = 0; index < value.entries.size(); ++index) {
        record.plugSources[index] = value.entries[index].plugSource;
        record.groups[index] = value.entries[index].group;
        record.kinds[index] = value.entries[index].kind;
    }
    return true;
}

bool decode(const SocketEntryTableRecord& record, socket_entry_lists::EntryTable& value) noexcept {
    value = {};
    if (record.reserved[0] != kReservedFieldValue || record.reserved[1] != kReservedFieldValue) {
        return false;
    }
    value.definitionIndex = record.definitionIndex;
    for (std::size_t index = 0; index < value.entries.size(); ++index) {
        value.entries[index].plugSource = record.plugSources[index];
        value.entries[index].group = record.groups[index];
        value.entries[index].kind = record.kinds[index];
    }
    return true;
}

} // namespace dawn::state::build_data::cache::records
