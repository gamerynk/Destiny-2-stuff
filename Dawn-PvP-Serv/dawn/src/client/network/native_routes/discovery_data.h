#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "../../../core/settings/address_text.h"

namespace dawn::client::hooks::egress::native_routes {

inline constexpr std::array<std::string_view, 20> kDiscoveryHosts{
    "stun.us.demonware.net", "stun.eu.demonware.net", "stun.jp.demonware.net",
    "stun.au.demonware.net", "stun101.signon.deadorbit.net",
    "stun104.signon.gravityshavings.net", "stun102.signon.deadorbit.net",
    "stun103.signon.gravityshavings.net", "stun103.signon.deadorbit.net",
    "stun102.signon.gravityshavings.net", "stun104.signon.deadorbit.net",
    "stun101.signon.gravityshavings.net", "stun105.signon.deadorbit.net",
    "stun105.signon.gravityshavings.net", "stun106.signon.deadorbit.net",
    "stun106.signon.gravityshavings.net", "stun107.signon.deadorbit.net",
    "stun107.signon.gravityshavings.net", "stun108.signon.deadorbit.net",
    "stun108.signon.gravityshavings.net"};

struct Plan final {
    std::array<std::size_t, kDiscoveryHosts.size()> offsets{};
};

[[nodiscard]] inline bool prepare(std::span<const char> image, std::string_view host,
                                  Plan& output) noexcept {
    std::array<unsigned char, 4> octets{};
    if (!core::settings::address::parse_ipv4(host, octets)) return false;
    Plan candidate{};
    for (std::size_t index = 0; index < kDiscoveryHosts.size(); ++index) {
        const auto literal = kDiscoveryHosts[index];
        if (host.size() > literal.size()) return false;
        bool found = false;
        auto begin = image.begin();
        while (begin != image.end()) {
            const auto match = std::search(begin, image.end(), literal.begin(), literal.end());
            if (match == image.end()) break;
            const auto offset = static_cast<std::size_t>(match - image.begin());
            if (offset + literal.size() < image.size() && image[offset + literal.size()] == '\0'
                && (offset == 0 || image[offset - 1] == '\0')) {
                if (found) return false;
                candidate.offsets[index] = offset;
                found = true;
            }
            begin = match + 1;
        }
        if (!found) return false;
    }
    output = candidate;
    return true;
}

[[nodiscard]] inline bool apply(std::span<char> image, std::string_view host,
                                const Plan& plan) noexcept {
    Plan checked{};
    if (!prepare(image, host, checked) || checked.offsets != plan.offsets) return false;
    for (std::size_t index = 0; index < kDiscoveryHosts.size(); ++index) {
        const auto offset = plan.offsets[index];
        std::fill_n(image.begin() + offset, kDiscoveryHosts[index].size() + 1U, '\0');
        std::copy(host.begin(), host.end(), image.begin() + offset);
    }
    return true;
}

} // namespace dawn::client::hooks::egress::native_routes
