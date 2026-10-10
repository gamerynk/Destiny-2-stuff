#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace dawn::client::ui::weapons::acquire_client::detail {
struct Selection {
    std::uint64_t requestId{}, characterSoid{};
    std::uint32_t weaponHash{};
    std::array<std::uint32_t, 12> plugs{};
    std::size_t count{};
};
// only an identical failed operation can reuse its id; success starts a new grant
[[nodiscard]] inline bool reuse_request(bool failed, const Selection& previous,
    std::uint64_t characterSoid, std::uint32_t weaponHash,
    std::span<const std::uint32_t> plugs) noexcept {
    return failed && previous.requestId != 0 && previous.characterSoid == characterSoid
        && previous.weaponHash == weaponHash && previous.count == plugs.size()
        && plugs.size() <= previous.plugs.size()
        && std::equal(plugs.begin(), plugs.end(), previous.plugs.begin());
}
}
