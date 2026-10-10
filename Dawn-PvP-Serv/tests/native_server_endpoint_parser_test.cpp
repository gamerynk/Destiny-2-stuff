#include <cassert>
#include <cstdio>
#include "core/settings/settings.h"
#define private public
#include "core/settings/parser.h"
#undef private
#include "../dawn/src/core/settings/server/server_settings_parser.cpp"
#include "../dawn/src/core/settings/server/entitlement_settings_parser.cpp"
#include "../dawn/src/core/settings/server/gameplay/gameplay_settings_parser.cpp"
#include "../dawn/src/core/settings/json_scalar.cpp"
#include "../dawn/src/core/settings/json_structure.cpp"
#include "../dawn/src/core/settings/json_number.cpp"


namespace dawn::core::settings::parser {
Parser::Parser(std::string_view input) noexcept : input_(input) {}
}
int main() {
    using namespace dawn::core::settings;
    server::Settings parsed{};
    auto valid = [&](std::string_view json) { return parser::Parser(json).server_settings(parsed); };
    assert(valid("{}") && !parsed.hasBapAdvertisedAddress && parsed.bapAdvertisedPort == 0);
    assert(valid(R"({"bap_advertised_address":"203.0.113.19","bap_advertised_port":40074})")
        && parsed.hasBapAdvertisedAddress && parsed.bapAdvertisedPort == 40074);
    for (const auto json : {
        R"({"bap_advertised_port":40074})",
        R"({"bap_advertised_address":"0.0.0.0"})",
        R"({"bap_advertised_address":"255.255.255.255"})",
        R"({"bap_advertised_address":"224.0.0.1"})",
        R"({"bap_advertised_address":"backend.example"})",
        R"({"bap_advertised_address":"203.0.113.19","bap_advertised_address":"192.0.2.1"})",
        R"({"bap_advertised_address":"203.0.113.19","bap_advertised_port":0})",
        R"({"bap_advertised_address":"203.0.113.19","bap_advertised_port":65536})",
        R"({"bap_advertised_address":"203.0.113.19","bap_advertised_port":1,"bap_advertised_port":2})"
    }) assert(!valid(json));
    std::puts("PASS: server endpoint settings reject unusable, ambiguous and incomplete destinations");
}
