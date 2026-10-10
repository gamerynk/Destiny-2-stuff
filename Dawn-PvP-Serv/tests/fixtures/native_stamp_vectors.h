#pragma once
#include <array>
#include <cstdint>
struct NativeStampVector {std::int32_t prior;std::uint16_t low;bool present;std::uint8_t cursor;std::int32_t stamp,nextHead;};
inline constexpr std::array<NativeStampVector,10> nativeStampVectors{{
{0,128,true,0,128,128},
{0,129,true,0,-895,0},
{896,0,true,0,1024,1024},
{895,0,true,0,0,895},
{1023,0,true,1,1024,1023},
{1023,0,false,0,-1,1024},
{2147483647,0,true,0,-2147483648,2147483647},
{-2147483648,1023,true,0,2147483647,2147483647},
{-1,0,true,127,0,-1},
{1024,0,false,127,-1,1024},
}};
