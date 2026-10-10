#include <limits>

#include "../../../state/entitlements/validation.h"
#include "../parser.h"
#include "../address_text.h"

namespace dawn::core::settings::parser {

bool Parser::server_settings(server::Settings& output) noexcept {
    output = {};
    output.entitlements = state::entitlements::authored();
    if (!consume('{')) {
        return false;
    }
    if (consume('}')) {
        return true;
    }
    bool hasEntitlements = false;
    bool hasBapPort = false;
    bool hasBapAdvertisedPort = false;
    bool hasHttpPort = false;
    bool hasPublicHttpPort = false;
    bool hasGameplay = false;
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "entitlements") {
            if (hasEntitlements || !entitlements(output.entitlements)) {
                return false;
            }
            hasEntitlements = true;
        } else if (key == "bap_port") {
            std::uint64_t value = 0;
            if (hasBapPort || !unsigned_integer(value) || value == 0
                || value > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            output.bapPort = static_cast<std::uint16_t>(value);
            hasBapPort = true;
        } else if (key == "bap_advertised_address") {
            std::string_view value;
            if (output.hasBapAdvertisedAddress || !string(value)
                || !address::parse_ipv4(value, output.bapAdvertisedAddress)
                || output.bapAdvertisedAddress[0] == 0
                || output.bapAdvertisedAddress[0] >= 224) {
                return false;
            }
            output.hasBapAdvertisedAddress = true;
        } else if (key == "bap_advertised_port") {
            std::uint64_t value = 0;
            if (hasBapAdvertisedPort || !unsigned_integer(value) || value == 0
                || value > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            output.bapAdvertisedPort = static_cast<std::uint16_t>(value);
            hasBapAdvertisedPort = true;
        } else if (key == "http_port") {
            std::uint64_t value = 0;
            if (hasHttpPort || !unsigned_integer(value) || value == 0
                || value > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            output.httpPort = static_cast<std::uint16_t>(value);
            hasHttpPort = true;
        } else if (key == "public_http_port") {
            std::uint64_t value = 0;
            if (hasPublicHttpPort || !unsigned_integer(value)
                || value > (std::numeric_limits<std::uint16_t>::max)()) {
                return false;
            }
            output.publicHttpPort = static_cast<std::uint16_t>(value);
            hasPublicHttpPort = true;
        } else if (key == "build_data_equipment_hash") {
            std::uint64_t value = 0;
            if (output.hasBuildDataEquipmentHash || !unsigned_value(value)) {
                return false;
            }
            output.buildDataEquipmentHash = value;
            output.hasBuildDataEquipmentHash = true;
        } else if (key == "gameplay") {
            if (hasGameplay || !gameplay_settings(output.gameplay)) {
                return false;
            }
            hasGameplay = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            return state::entitlements::valid(output.entitlements)
                   && (!hasBapAdvertisedPort || output.hasBapAdvertisedAddress)
                   && (output.publicHttpPort == 0
                       || output.publicHttpPort != output.httpPort);
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace dawn::core::settings::parser
