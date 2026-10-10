#include "introduction.h"

namespace dawn::middleware::gameplay::nat {

namespace {

constexpr std::size_t kTypeOffset = 0;
constexpr std::byte kRequestType{13};
constexpr std::byte kReplyType{12};

} // namespace

bool make_introduction_reply(std::span<std::byte> datagram) noexcept {
    if (datagram.size() != kIntroductionSize || datagram[kTypeOffset] != kRequestType) {
        return false;
    }
    datagram[kTypeOffset] = kReplyType;
    return true;
}

} // namespace dawn::middleware::gameplay::nat
