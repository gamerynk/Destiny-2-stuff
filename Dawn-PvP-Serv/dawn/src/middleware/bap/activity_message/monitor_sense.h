#pragma once
#include <cstdint>

namespace dawn::middleware::bap::activity_message::monitor_sense {
struct Output final {
    bool any{},all{};
    std::int32_t count{},value{};
};
template<class Reader> bool read(Reader& reader,Output& output) noexcept {
    std::uint64_t any{},all{},count{},value{};
    if(!reader.read(1,any) || !reader.read(1,all)
        || !reader.read(32,count) || !reader.read(32,value)) { return false; }
    output={any!=0,all!=0,static_cast<std::int32_t>(static_cast<std::int64_t>(count)-0x80000000LL),
        static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x80000000LL)};
    return true;
}
}
