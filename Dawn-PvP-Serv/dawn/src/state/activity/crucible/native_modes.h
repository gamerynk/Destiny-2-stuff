#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawn::state::activity::crucible {
struct NativeMode final {
    std::int16_t activityIndex;
    std::uint32_t activityHash;
    std::uint32_t symbolicHash;
    std::uint32_t policyTag;
    std::uint32_t optionsTag;
    std::string_view symbolicName;
};
inline constexpr std::array<NativeMode, 53> kNativeModes{{
    {483, 0xC14EB626U, 0x0072D303U, 0x813206D9U, 0x00000000U, "pvp_control"},
    {484, 0xB6856D70U, 0x0072D303U, 0x813206DAU, 0x00000000U, "pvp_control"},
    {485, 0x91BA11ADU, 0x6222AAECU, 0x813206DBU, 0x00000000U, "pvp_doubles_crimson"},
    {486, 0x1E1137DCU, 0xFF9A4D0AU, 0x813206DBU, 0x00000000U, "pvp_doubles"},
    {487, 0x35DB9C95U, 0xD1A4037BU, 0x813206DCU, 0x00000000U, "pvp_mayhem"},
    {488, 0x4E220E95U, 0xC3122B73U, 0x813206D8U, 0x00000000U, "pvp_rumble"},
    {489, 0x36B5DAE7U, 0x0072D303U, 0x813206D9U, 0x00000000U, "pvp_control"},
    {490, 0x8F52275DU, 0x09706B50U, 0x813206DEU, 0x813206DDU, "pvp_breakthrough"},
    {491, 0xE08D5B0BU, 0xC0665102U, 0x813206DEU, 0x813206DDU, "pvp_demolition"},
    {492, 0xE3029D28U, 0x0261283FU, 0x813206DEU, 0x813206DDU, "pvp_elimination"},
    {493, 0x6506AEA8U, 0x8FB39F7DU, 0x813206DEU, 0x813206DDU, "pvp_lockdown"},
    {494, 0x707F616AU, 0xC2D1047FU, 0x813206DEU, 0x813206DDU, "pvp_showdown"},
    {495, 0xAD15B067U, 0x81B2721EU, 0x813206DEU, 0x813206DDU, "pvp_survival"},
    {496, 0x48993C02U, 0x05BEE535U, 0x813206E0U, 0x813206DFU, "pvp_supremacy"},
    {497, 0xFCDFCCCCU, 0xEBB81A5FU, 0x813206E0U, 0x813206DFU, "pvp_clash"},
    {498, 0x6ED5D40CU, 0x0072D303U, 0x813206E0U, 0x813206DFU, "pvp_control"},
    {499, 0x2E988C2FU, 0xD73B11D2U, 0x813206E0U, 0x813206DFU, "pvp_momentum"},
    {500, 0x7882E199U, 0x84941C2CU, 0x813206E0U, 0x813206DFU, "pvp_hardware"},
    {501, 0x01C5971CU, 0xD1A4037BU, 0x813206E0U, 0x813206DFU, "pvp_mayhem"},
    {502, 0x94872416U, 0xC3122B73U, 0x813206E1U, 0x813206DFU, "pvp_rumble"},
    {503, 0xC758212EU, 0x267F53E7U, 0x813206E1U, 0x813206DFU, "pvp_scorched"},
    {504, 0x9296B792U, 0x1EDD6DDDU, 0x813206E0U, 0x813206DFU, "pvp_scorched_team"},
    {505, 0x22E35108U, 0xEBB81A5FU, 0x813206D7U, 0x00000000U, "pvp_clash"},
    {506, 0x878D2015U, 0x0072D303U, 0x813206D9U, 0x00000000U, "pvp_control"},
    {507, 0xAFA952AFU, 0x0072D303U, 0x813206E2U, 0x00000000U, "pvp_control"},
    {508, 0x7C679701U, 0xC3122B73U, 0x813206D8U, 0x00000000U, "pvp_rumble"},
    {509, 0xDFB9F3F5U, 0x6B3B6C21U, 0x813206E3U, 0x00000000U, "pvp_control_iron_banner"},
    {510, 0x7AFFAB48U, 0xFF9A4D0AU, 0x813206DBU, 0x00000000U, "pvp_doubles"},
    {511, 0x4E3F8A09U, 0xD1A4037BU, 0x813206D9U, 0x00000000U, "pvp_mayhem"},
    {512, 0x68145723U, 0x05BEE535U, 0x813206D9U, 0x00000000U, "pvp_supremacy"},
    {513, 0x4C197F36U, 0x09706B50U, 0x813206D7U, 0x813206E4U, "pvp_breakthrough"},
    {514, 0xC14FF9EBU, 0x8FB39F7DU, 0x813206D7U, 0x00000000U, "pvp_lockdown"},
    {515, 0x449FEDADU, 0xC2D1047FU, 0x813206D7U, 0x00000000U, "pvp_showdown"},
    {516, 0x50F55559U, 0x6222AAECU, 0x813206DBU, 0x00000000U, "pvp_doubles_crimson"},
    {517, 0xBD563A0CU, 0x0072D303U, 0x813206E6U, 0x00000000U, "pvp_control"},
    {518, 0x8953265EU, 0xEBB81A5FU, 0x813206E7U, 0x00000000U, "pvp_clash"},
    {519, 0xE14FAEC8U, 0x0072D303U, 0x813206E7U, 0x00000000U, "pvp_control"},
    {520, 0x48A9BD06U, 0x1EDD6DDDU, 0x813206E7U, 0x00000000U, "pvp_scorched_team"},
    {521, 0xFAE3B2C9U, 0x0072D303U, 0x813206E7U, 0x00000000U, "pvp_control"},
    {522, 0x38CC2C83U, 0xD73B11D2U, 0x813206E7U, 0x00000000U, "pvp_momentum"},
    {523, 0x41B4F83EU, 0xD1A4037BU, 0x813206E7U, 0x00000000U, "pvp_mayhem"},
    {524, 0x895ECE9BU, 0x0072D303U, 0x813206E8U, 0x00000000U, "pvp_control"},
    {525, 0x08142329U, 0x81B2721EU, 0x813206EAU, 0x813206E9U, "pvp_survival"},
    {526, 0x2C291AC1U, 0x81B2721EU, 0x813206EAU, 0x813206E9U, "pvp_survival"},
    {527, 0x581B1BDCU, 0x0261283FU, 0x813206EBU, 0x813206E9U, "pvp_elimination"},
    {528, 0xB37D2AC9U, 0x09706B50U, 0x813206ECU, 0x00000000U, "pvp_breakthrough"},
    {529, 0xD952C11CU, 0xA38AEE6DU, 0x813206ECU, 0x00000000U, "pvp_countdown"},
    {530, 0x56D928B2U, 0xC2D1047FU, 0x813206ECU, 0x00000000U, "pvp_showdown"},
    {531, 0xC111B900U, 0x8FB39F7DU, 0x813206ECU, 0x00000000U, "pvp_lockdown"},
    {532, 0x2C21F2BFU, 0x81B2721EU, 0x813206ECU, 0x00000000U, "pvp_survival"},
    {533, 0x36E2B21BU, 0xFF9A4D0AU, 0x813206EDU, 0x00000000U, "pvp_doubles"},
    {534, 0x367CCB47U, 0xC3122B73U, 0x813206D8U, 0x00000000U, "pvp_rumble"},
    {535, 0xD899253FU, 0x6222AAECU, 0x813206DBU, 0x00000000U, "pvp_doubles_crimson"},
}};
[[nodiscard]] constexpr const NativeMode* find_native_mode(std::int16_t index) noexcept {
    if (index < 483 || index >= 536) return nullptr;
    return &kNativeModes[static_cast<std::size_t>(index - 483)];
}
} // namespace dawn::state::activity::crucible
