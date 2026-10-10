#include <cstddef>
#include <limits>
#include <string_view>

#include "../../address_text.h"
#include "../../parser.h"

namespace dawn::core::settings::parser {
namespace {

namespace external = client::external;

constexpr unsigned int kMinimumPrintableAscii = 0x20;
constexpr unsigned int kMaximumPrintableAscii = 0x7E;

// JSON escapes are refused rather than decoded; no supported value needs one
template <std::size_t Capacity>
[[nodiscard]] bool decode_text(std::string_view encoded,
                               std::array<char, Capacity>& output) noexcept {
    if (encoded.empty() || encoded.size() >= Capacity) {
        return false;
    }
    std::array<char, Capacity> candidate{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        const auto byte = static_cast<unsigned char>(encoded[index]);
        if (byte < kMinimumPrintableAscii || byte > kMaximumPrintableAscii || byte == '\\') {
            return false;
        }
        candidate[index] = encoded[index];
    }
    output = candidate;
    return true;
}

// names are refused because the resolver forwards this text with AI_NUMERICHOST
[[nodiscard]] bool derive_address(external::Settings& output) noexcept {
    const std::string_view text(output.host.data());
    if (!address::parse_ipv4(text, output.address)) {
        return false;
    }
    output.hostWide = {};
    for (std::size_t index = 0; index < text.size(); ++index) {
        output.hostWide[index] = static_cast<wchar_t>(text[index]);
    }
    return true;
}

} // namespace

bool Parser::client_external_settings(external::Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    external::Settings candidate = output;
    bool hasEnabled = false;
    bool hasHost = false;
    bool hasOrigin = false;
    bool hasHttpPort = false;
    bool hasConfigUrl = false;
    bool hasPortRoutes = false;
    if (consume('}')) {
        return true;
    }
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "enabled") {
            if (hasEnabled || !boolean(candidate.enabled)) {
                return false;
            }
            hasEnabled = true;
        } else if (key == "host") {
            std::string_view value;
            if (hasHost || !string(value) || !decode_text(value, candidate.host)
                || !derive_address(candidate)) {
                return false;
            }
            hasHost = true;
        } else if (key == "origin") {
            std::string_view value;
            if (hasOrigin || !string(value) || !decode_text(value, candidate.origin)) {
                return false;
            }
            hasOrigin = true;
        } else if (key == "http_port") {
            std::uint64_t value = 0;
            if (hasHttpPort || !unsigned_integer(value) || value == 0
                || value > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            candidate.httpPort = static_cast<std::uint16_t>(value);
            hasHttpPort = true;
        } else if (key == "config_url") {
            std::string_view value;
            if (hasConfigUrl || !string(value) || !decode_text(value, candidate.configUrl)) {
                return false;
            }
            hasConfigUrl = true;
        } else if (key == "port_routes") {
            if (hasPortRoutes || !consume('[')) {
                return false;
            }
            candidate.portRoutes = {};
            candidate.portRouteCount = 0;
            if (!consume(']')) {
                for (;;) {
                    std::uint64_t sourcePort = 0;
                    std::uint64_t targetPort = 0;
                    std::string_view host;
                    if (candidate.portRouteCount >= candidate.portRoutes.size() || !consume('[')
                        || !unsigned_integer(sourcePort) || !consume(',') || !string(host)
                        || !consume(',') || !unsigned_integer(targetPort) || !consume(']')
                        || sourcePort == 0
                        || sourcePort > (std::numeric_limits<std::uint16_t>::max)()
                        || targetPort == 0
                        || targetPort > (std::numeric_limits<std::uint16_t>::max)()) {
                        return false;
                    }

                    external::PortRoute route{};
                    route.sourcePort = static_cast<std::uint16_t>(sourcePort);
                    route.targetPort = static_cast<std::uint16_t>(targetPort);
                    if (!address::parse_ipv4(host, route.address)) {
                        return false;
                    }
                    for (std::size_t index = 0; index < candidate.portRouteCount; ++index) {
                        if (candidate.portRoutes[index].sourcePort == route.sourcePort) {
                            return false;
                        }
                    }
                    candidate.portRoutes[candidate.portRouteCount++] = route;

                    if (consume(']')) {
                        break;
                    }
                    if (!consume(',')) {
                        return false;
                    }
                }
            }
            hasPortRoutes = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            output = candidate;
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace dawn::core::settings::parser
