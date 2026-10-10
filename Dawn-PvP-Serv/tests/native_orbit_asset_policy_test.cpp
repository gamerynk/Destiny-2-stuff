#include <filesystem>
#include <fstream>
#include <cstdio>
#include "client/hooks/bootflow/native_orbit_asset_policy.h"

namespace p=dawn::client::hooks::bootflow::native_orbit_asset_policy;
namespace fs=std::filesystem;
static bool check(bool ok,const char* name) {
    if (!ok) std::fprintf(stderr,"FAIL: %s\n",name);
    return ok;
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=3) return 2;
    const fs::path staged=argv[1],scratch=argv[2];
    fs::create_directories(scratch);
    if (!fs::is_empty(scratch)) return 3;
    bool ok=true;
    for (const auto& asset:p::kAssets)
        fs::copy_file(staged/asset.filename,scratch/asset.filename);
    const auto directory=scratch.wstring()+L"\\";
    const bool identityMatches=p::matches_directory(directory);
    ok&=check(identityMatches,"exact paired authored package identity recognized");
    ok&=check(p::omission_allowed_for_assets(identityMatches),
        "exact authored pair with clean-cache run16 live validation allows omission");
    ok&=check(!p::omission_allowed_for_assets(false),
        "missing or changed assets cannot omit picker");
    const auto first=scratch/p::kAssets[0].filename;
    {
        std::fstream stream(first,std::ios::in|std::ios::out|std::ios::binary);
        char byte{};stream.seekg(24576);stream.read(&byte,1);byte^=1;
        stream.seekp(24576);stream.write(&byte,1);
    }
    ok&=check(!p::matches_directory(directory),"same-size changed native package rejected");
    fs::copy_file(staged/p::kAssets[0].filename,first,fs::copy_options::overwrite_existing);
    {
        std::fstream stream(first,std::ios::in|std::ios::out|std::ios::binary);
        char byte{};stream.seekg(0x118);stream.read(&byte,1);byte^=1;
        stream.seekp(0x118);stream.write(&byte,1);
    }
    ok&=check(!p::matches_directory(directory),"unqualified extended digest identity rejected");
    fs::copy_file(staged/p::kAssets[0].filename,first,fs::copy_options::overwrite_existing);
    const auto newer=scratch/L"w64_orbit_d2_03b1_6.pkg";
    fs::copy_file(first,newer);
    ok&=check(!p::matches_directory(directory),"newer patch superseding authored entries rejected");
    fs::remove(newer);
    const auto huge=scratch/L"w64_orbit_d2_03b1_999999999999999999.pkg";
    fs::copy_file(first,huge);
    ok&=check(!p::matches_directory(directory),"patch integer overflow cannot admit alternate metadata");
    fs::remove(huge);
    fs::resize_file(first,fs::file_size(first)-1);
    ok&=check(!p::matches_directory(directory),"short native package rejected");
    fs::copy_file(staged/p::kAssets[0].filename,first,fs::copy_options::overwrite_existing);
    fs::remove(scratch/p::kAssets[1].filename);
    ok&=check(!p::matches_directory(directory),"missing state carrier package rejects omission");
    fs::remove(first);
    if (ok) std::puts("PASS: exact native Orbit asset pair and compatibility fallback boundaries");
    return ok?0:1;
}
