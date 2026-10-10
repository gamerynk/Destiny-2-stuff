#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::gameplay::nat {

inline constexpr std::size_t kIntroductionSize = 29;

// only the type byte changes, so the requester gets its own token and target hash back;
// send the reply from the address the request arrived at
[[nodiscard]] bool make_introduction_reply(std::span<std::byte> datagram) noexcept;

} // namespace dawn::middleware::gameplay::nat
