#include <Windows.h>

#include <array>

#include "../../../../core/filesystem/path.h"
#include "../../../../state/content/content_catalog.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../memory/current_process_memory.h"
#include "../../../targets/game.h"
#include "build.h"
#include "internal.h"

namespace dawn::client::content::items::packages {
namespace {

constexpr std::wstring_view kPackageDirectory = L"packages";
constexpr std::uint32_t kInvestmentGlobalsNameHash = 0x6F7125CBU;

} // namespace

bool readable() noexcept {
    reader::BlockKeys keys{};
    const bool available = collect_keys(keys);
    SecureZeroMemory(&keys, sizeof keys);
    return available;
}

bool collect_keys(reader::BlockKeys& keys) noexcept {
    keys = {};
    const state::SignOnState& signOn = state::sign_on();
    targets::game::packages::KeyTable table{};
    if (!signOn.bootstrapTokenPresent) {
        return false;
    }
    const bool read = targets::game::packages::read(table);
    if (!read) {
        SecureZeroMemory(&table, sizeof table);
        return false;
    }
    keys.alternate = table.alternateKey;
    keys.nonceBase = table.nonceBase;
    // primary key comes from the bootstrap token and installed identity constant, not the native buffer
    for (std::size_t index = 0; index < keys.primary.size(); ++index) {
        const auto tokenByte = static_cast<unsigned char>(signOn.bootstrapToken[index]);
        const auto constantByte = static_cast<unsigned char>(table.identityConstant[index]);
        keys.primary[index] = static_cast<std::byte>(tokenByte + constantByte);
    }
    SecureZeroMemory(&table, sizeof table);
    return true;
}

bool investment_globals_tags(std::array<std::uint32_t, kContainerCandidates>& candidates,
                             std::size_t& count) noexcept {
    candidates = {};
    count = 0;
    std::array<state::content::Definition, kContainerCandidates> matches{};
    std::size_t matched = 0;
    if (!state::content::lookup_hash(kInvestmentGlobalsNameHash, matches, matched)) {
        return false;
    }
    for (std::size_t index = 0; index < matched; ++index) {
        if (matches[index].tag != 0) {
            candidates[count++] = matches[index].tag;
        }
    }
    return count != 0;
}

bool package_directory(core::path::Buffer& directory) noexcept {
    if (!core::path::module_directory(GetModuleHandleW(nullptr), directory)
        || !core::path::append(directory, kPackageDirectory)) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(directory.chars.data());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace dawn::client::content::items::packages
