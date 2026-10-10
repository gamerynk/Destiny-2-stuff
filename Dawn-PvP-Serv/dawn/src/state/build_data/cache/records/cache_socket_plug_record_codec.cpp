#include "codec.h"

namespace dawn::state::build_data::cache::records {

bool encode(const items::socket_plugs::Rule& value, SocketPlugRuleRecord& record) noexcept {
    record = {};
    if (value.reserved != 0) {
        return false;
    }
    record.itemDefinitionIndex = value.itemDefinitionIndex;
    record.lane = value.lane;
    record.poolIndex = value.poolIndex;
    return true;
}

bool decode(const SocketPlugRuleRecord& record, items::socket_plugs::Rule& value) noexcept {
    value = {};
    if (record.reserved != 0) {
        return false;
    }
    value.itemDefinitionIndex = record.itemDefinitionIndex;
    value.lane = record.lane;
    value.poolIndex = record.poolIndex;
    return true;
}

bool encode(const items::socket_plugs::Pool& value, SocketPlugPoolRecord& record) noexcept {
    record = {value.memberOffset, value.memberCount};
    return true;
}

bool decode(const SocketPlugPoolRecord& record, items::socket_plugs::Pool& value) noexcept {
    value = {record.memberOffset, record.memberCount};
    return true;
}

bool encode(items::socket_plugs::Member value, SocketPlugMemberRecord& record) noexcept {
    record = {value};
    return true;
}

bool decode(const SocketPlugMemberRecord& record, items::socket_plugs::Member& value) noexcept {
    value = record.itemDefinitionIndex;
    return true;
}

} // namespace dawn::state::build_data::cache::records
