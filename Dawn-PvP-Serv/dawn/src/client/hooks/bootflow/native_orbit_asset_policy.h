#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include "../../../middleware/crypto/sha256.h"

namespace dawn::client::hooks::bootflow::native_orbit_asset_policy {

// run16/build14 validated this exact authored pair with a fresh native package cache; the
// approval covers only that pair, never arbitrary matching shapes
inline constexpr bool kFunctionalEquivalenceValidated=true;
[[nodiscard]] inline constexpr bool omission_allowed_for_assets(bool assetsMatch) noexcept {
    return kFunctionalEquivalenceValidated && assetsMatch;
}

struct Asset final {
    const wchar_t* filename;
    const wchar_t* pattern;
    std::uint32_t bytes;
    std::array<std::uint8_t,32> digest;
    std::array<std::uint8_t,32> previousDigest;
};
inline constexpr std::array<Asset,2> kAssets{{
    {L"w64_orbit_d2_03b1_5.pkg",L"w64_orbit_d2_03b1_*.pkg",86016,
     {0x39,0x81,0x5f,0x00,0x12,0x3d,0x83,0x69,0x98,0x58,0x6c,0xba,0x74,0x29,0x0f,0xdf,0x74,0xa0,0x03,0xe2,0xbd,0x95,0xa1,0xde,0x82,0xa2,0x2c,0xc8,0x16,0xde,0xc2,0xf4},
     {0xb2,0x18,0xf7,0xa6,0x4a,0xe4,0xd8,0x81,0xce,0xd0,0x3e,0x3c,0x62,0xf7,0x90,0x77,0x33,0x9a,0xcf,0xcf,0x5d,0x21,0x37,0x97,0xf0,0x45,0x4f,0xdc,0xf3,0xbc,0x8e,0xf0}},
    {L"w64_orbit_d2_activities_01c3_5.pkg",L"w64_orbit_d2_activities_01c3_*.pkg",57344,
     {0x26,0x6f,0x56,0xfe,0x10,0x02,0xe5,0x4a,0x2a,0x8d,0x48,0xe1,0x87,0x7c,0x81,0x07,0xee,0x46,0xf3,0x5c,0x6c,0x14,0x92,0x68,0x92,0xde,0x1d,0x9c,0x73,0x9d,0x12,0x03},
     {0x7a,0x42,0xf8,0xbf,0x32,0x6f,0xdf,0x64,0x29,0x66,0x4d,0x8a,0xd4,0x41,0xae,0x08,0x4b,0x9a,0x69,0xfa,0x3f,0x3d,0xfa,0x32,0xd9,0x8d,0x20,0xef,0x04,0x28,0x73,0x6b}}
}};

[[nodiscard]] inline bool file_matches(const wchar_t* path, const Asset& asset) noexcept {
    const HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    bool valid=false;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file,&size) && size.QuadPart==asset.bytes) {
        try {
            std::vector<std::byte> bytes(asset.bytes);
            DWORD read{};
            middleware::crypto::sha256::Digest hash{};
            valid=ReadFile(file,bytes.data(),asset.bytes,&read,nullptr) && read==asset.bytes
                  && middleware::crypto::sha256::hash(bytes,hash);
            if (valid) {
                bool corrected=true,previous=true;
                for (std::size_t i=0;i<hash.size();++i) {
                    const auto byte=std::to_integer<std::uint8_t>(hash[i]);
                    corrected&=byte==asset.digest[i];
                    previous&=byte==asset.previousDigest[i];
                }
                valid=corrected || previous;
            }
        } catch (...) { valid=false; }
    }
    CloseHandle(file);
    return valid;
}

[[nodiscard]] inline bool highest_patch_is_five(const std::wstring& pattern) noexcept {
    WIN32_FIND_DATAW entry{};
    HANDLE scan=FindFirstFileW(pattern.c_str(),&entry);
    if (scan==INVALID_HANDLE_VALUE) return false;
    bool valid=true,found=false;
    do {
        if ((entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0) continue;
        const wchar_t* part=std::wcsrchr(entry.cFileName,L'_');
        if (!part) { valid=false; break; }
        ++part;
        unsigned patch=0;
        if (*part<L'0'||*part>L'9') { valid=false; break; }
        while (*part>=L'0'&&*part<=L'9') {
            if (patch>5) break;
            patch=patch*10+static_cast<unsigned>(*part++-L'0');
        }
        if (patch>5 || std::wcscmp(part,L".pkg")!=0) { valid=false; break; }
        found|=patch==5;
    } while (FindNextFileW(scan,&entry));
    const DWORD status=GetLastError();
    FindClose(scan);
    return valid && found && status==ERROR_NO_MORE_FILES;
}

[[nodiscard]] inline bool matches_directory(const std::wstring& directory) noexcept {
    try {
        for (const auto& asset:kAssets)
            if (!highest_patch_is_five(directory+asset.pattern)
                || !file_matches((directory+asset.filename).c_str(),asset)) return false;
        return true;
    } catch (...) { return false; }
}

[[nodiscard]] inline bool installed_assets_match() noexcept {
    try {
        std::wstring path(32768,L'\0');
        const DWORD size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if (size==0 || size>=path.size()) return false;
        path.resize(size);
        const auto slash=path.find_last_of(L"\\/");
        if (slash==std::wstring::npos) return false;
        path.resize(slash+1);path+=L"packages\\";
        return matches_directory(path);
    } catch (...) { return false; }
}

} // namespace dawn::client::hooks::bootflow::native_orbit_asset_policy
