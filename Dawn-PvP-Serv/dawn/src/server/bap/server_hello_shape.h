#pragma once
#include <cstddef>
#include <span>
#include "middleware/protobuf/codec.h"

namespace dawn::server::bap::hello_shape {
// framing only: validation never authenticates a token or selects an account
inline bool valid(std::span<const std::byte> body) noexcept {
    if (body.size()<36 || body[0]!=std::byte{0x0A} || body[1]!=std::byte{0x20}
        || body[34]!=std::byte{0x10}) return false;
    const auto version=body[35];
    if (version==std::byte{1}) return body.size()==36;
    if (version==std::byte{2}) {
        if (body.size()==36) return true;
        if (body.size()!=47) return false;
        // schema141C16320: optional tag 3 is a uint64 varint, seen from joined follower PID3012
        // a routing value, not a credential; it cannot change the account binding
        middleware::protobuf::Reader fields(body.subspan(36));
        middleware::protobuf::Field field{};
        return fields.next(field) && field.fieldNumber==3
            && field.wireType==middleware::protobuf::WireType::varint && fields.remaining()==0;
    }
    return version==std::byte{3} && body.size()==47;
}
}
