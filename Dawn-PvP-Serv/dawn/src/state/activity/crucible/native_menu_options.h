#pragma once
#include <array>
#include <cstdint>
namespace dawn::state::activity::crucible::menu_rules {
struct Definition { std::int16_t index; bool seconds; std::uint8_t count;
    std::array<std::uint32_t,15> values; };
inline constexpr std::array<Definition,14> kDefinitions{{
    {22, false, 6, {25,50,75,100,150,200}},
    {23, true, 6, {300,480,600,900,1200,1800}},
    {26, false, 6, {25,50,75,100,150,200}},
    {27, true, 6, {300,480,600,900,1200,1800}},
    {29, false, 6, {25,50,75,100,150,200}},
    {30, true, 6, {300,480,600,900,1200,1800}},
    {60, true, 6, {300,480,600,900,1200,1800}},
    {61, false, 6, {25,75,125,250,300,500}},
    {64, false, 6, {10,25,50,75,100,125}},
    {65, true, 6, {300,480,600,900,1200,1800}},
    {67, false, 6, {10,25,75,100,125,250}},
    {68, true, 6, {300,480,600,900,1200,1800}},
    {70, false, 6, {10,25,75,100,125,250}},
    {71, true, 6, {300,480,600,900,1200,1800}},
}};
}
