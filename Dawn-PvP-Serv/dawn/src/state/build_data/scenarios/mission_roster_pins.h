#pragma once

#include <cstdint>
#include <string_view>

namespace dawn::state::build_data::scenarios::mission_roster_pins {

// roster groups a server mission publishes that the destination walk filters out (squads, devices,
// volumes, scenes). Tags, keys and bubbles come from uex's strike_pact_roster.h for the accepted Sunrise host
struct Pin final {
    std::string_view destination;
    std::uint32_t objectTag;
    std::uint32_t key;
    std::uint8_t bubble;
    bool topLevel;
    std::string_view name;
};

inline constexpr Pin kPins[]{
    {"strike_pact", 0x80F55229U, 0x361407B2U, 0, true, "presentation root"},
    {"strike_pact", 0x80F551DEU, 0xCC7A090DU, 15, false, "lighthouse opening"},
    {"strike_pact", 0x80F551EBU, 0x75A69C5AU, 15, false, "lighthouse teleporter"},
    {"strike_pact", 0x80F55205U, 0xF8D0DADCU, 15, false, "lighthouse area and tunnel"},
    {"strike_pact", 0x80F550B8U, 0x2763EC91U, 9, false, "infinite forest b"},
    {"strike_pact", 0x80F550C9U, 0xA9350228U, 9, false, "forest portal triggers"},
    {"strike_pact", 0x80F550DAU, 0x7E558786U, 9, false, "forest exit"},
    {"strike_pact", 0x80F54FFEU, 0x588E5FB9U, 2, false, "chase"},
    {"strike_pact", 0x80F55018U, 0xFBD01A06U, 2, false, "chase triggers"},
    {"strike_pact", 0x80F55029U, 0x8E64DB66U, 2, false, "chase entry"},
    {"strike_pact", 0x80F54E07U, 0xA5F083B5U, 0, false, "ledge, boss rooms and ending"},
    {"strike_pact", 0x80F54E12U, 0x73CBF939U, 0, false, "boss approach"},
    {"strike_pact", 0x80F54E1DU, 0xC6F46FAFU, 0, false, "boss room points"},
};

[[nodiscard]] constexpr bool pinned_object(std::uint32_t objectTag) noexcept {
    for (const Pin& pin : kPins) {
        if (pin.objectTag == objectTag) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool has_pins(std::string_view destination) noexcept {
    for (const Pin& pin : kPins) {
        if (pin.destination == destination) {
            return true;
        }
    }
    return false;
}

} // namespace dawn::state::build_data::scenarios::mission_roster_pins
