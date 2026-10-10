#include "package_trust_bypass.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"

namespace dawn::client::hooks::package_trust {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

#if defined(RESTORATION_NATIVE_PACKAGE_EXTENDED_HASH) && RESTORATION_NATIVE_PACKAGE_EXTENDED_HASH
constexpr bool kNativeExtendedHash=true;
#else
constexpr bool kNativeExtendedHash=false;
#endif
#if defined(RESTORATION_NATIVE_PACKAGE_CACHED_HASH) && RESTORATION_NATIVE_PACKAGE_CACHED_HASH
constexpr bool kNativeCachedHash=true;
#else
constexpr bool kNativeCachedHash=false;
#endif

// native package-header validator: the prologue tests its sixth argument and returns -93
// when RSA verification did not mark the header trusted
constexpr std::string_view kValidatorSignatureText =
    "40 53 48 83 EC 20 80 7C 24 58 00 44 0F B7 DA 4C 8B D1 BB 01 00 00 00 "
    "75 0D BB A3 FF FF FF 8B C3 48 83 C4 20 5B C3";
constexpr auto kValidatorSignature =
    signature<signature_length(kValidatorSignatureText)>(kValidatorSignatureText);

// extended-header authentication failure: native code loads -89 here, then joins the
// common result/cleanup path; unique in this client build
constexpr std::string_view kExtendedHeaderFailureText = "B8 A7 FF FF FF E9 ? ? ? ?";
constexpr auto kExtendedHeaderFailure =
    signature<signature_length(kExtendedHeaderFailureText)>(kExtendedHeaderFailureText);

// cached-data auth gate: the hash routine returns a bool in AL; failure takes the unique
// "Failed to validate cached data hash" path with -89
constexpr std::string_view kCachedDataHashGateText =
    "84 C0 0F 85 ? ? ? ? E9 ? ? ? ? 48 8B 45 48 89 08";
constexpr auto kCachedDataHashGate =
    signature<signature_length(kCachedDataHashGateText)>(kCachedDataHashGateText);

constexpr std::size_t kResultImmediateOffset = 1;
constexpr std::array<std::byte, 4> kSuccessResult{
    std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

// JNZ rel32 (0F 85) becomes NOP + JMP rel32 (90 E9), keeping the native destination
constexpr std::size_t kCachedDataBranchOffset = 2;
constexpr std::array<std::byte, 2> kAlwaysTakeSuccessBranch{std::byte{0x90}, std::byte{0xE9}};

using ValidateHeader = std::int32_t(__fastcall*)(const std::uint32_t* validationMask,
                                                 std::uint16_t packageGroup,
                                                 std::uint64_t buildSignature,
                                                 std::int32_t expectedFileSize,
                                                 std::uint16_t localeToken,
                                                 std::uint8_t rsaTrusted,
                                                 const void* header) noexcept;

hooking::detour::Handle g_handle{};
bool g_installComplete{};
std::byte* g_extendedHeaderResult{};
std::array<std::byte, kSuccessResult.size()> g_extendedHeaderOriginal{};
DWORD g_extendedHeaderProtection{};
bool g_extendedHeaderProtectionSaved{};
std::byte* g_cachedDataBranch{};
std::array<std::byte, kAlwaysTakeSuccessBranch.size()> g_cachedDataBranchOriginal{};
DWORD g_cachedDataProtection{};
bool g_cachedDataProtectionSaved{};

#if defined(RESTORATION_PACKAGE_TRUST_PROBE) && RESTORATION_PACKAGE_TRUST_PROBE
volatile LONG g_nativeTrustProbeLines[5]{};
void probe_native_trust(std::uint16_t packageId, std::uint16_t patchId,
                        std::uint8_t nativeTrust) noexcept {
    const unsigned group=packageId==0x03B1 ? 0U : packageId==0x01C3 ? 1U
        : packageId==0x0590 ? 2U : nativeTrust==0 ? 3U : 4U;
    const LONG limit=group<3 ? 4 : 16;
    if (InterlockedIncrement(&g_nativeTrustProbeLines[group])>limit) return;
    std::array<char,192> line{};
    const int size=std::snprintf(line.data(),line.size(),
        "ev=package_trust stage=native_rsa_input package=0x%04X patch=%u producer_trusted=%u existing_override=1",
        packageId,patchId,nativeTrust);
    if (size>0 && static_cast<std::size_t>(size)<line.size())
        core::log::write(core::log::Channel::client,core::log::Level::info,
            {line.data(),static_cast<std::size_t>(size)});
}
#endif

template <std::size_t Size>
[[nodiscard]] bool write_code(std::byte* destination,
                              const std::array<std::byte, Size>& value,
                              DWORD& savedProtection,
                              bool& protectionSaved) noexcept {
    if (destination == nullptr) {
        return false;
    }
    DWORD originalProtection = 0;
    if (VirtualProtect(destination, value.size(), PAGE_EXECUTE_READWRITE, &originalProtection)
        == FALSE) {
        return false;
    }
    // a failed protection restore leaves the page writable; keep the first protection across
    // retry so the writable one isn't mistaken for the original
    if (!protectionSaved) {
        savedProtection = originalProtection;
        protectionSaved = true;
    }
    std::memcpy(destination, value.data(), value.size());
    FlushInstructionCache(GetCurrentProcess(), destination, value.size());
    DWORD ignored = 0;
    return VirtualProtect(destination, value.size(), savedProtection, &ignored) != FALSE;
}

std::int32_t __fastcall validate_header(const std::uint32_t* validationMask,
                                        std::uint16_t packageGroup,
                                        std::uint64_t buildSignature,
                                        std::int32_t expectedFileSize,
                                        std::uint16_t localeToken,
                                        std::uint8_t nativeTrust,
                                        const void* header) noexcept {
    if (header != nullptr) {
        const auto* const bytes = static_cast<const std::byte*>(header);
        std::uint16_t packageId = 0;
        std::uint16_t patchId = 0;
        std::uint32_t headerFileSize = 0;
        std::memcpy(&packageId, bytes + 0x04, sizeof packageId);
        std::memcpy(&patchId, bytes + 0x20, sizeof patchId);
        std::memcpy(&headerFileSize, bytes + 0x164, sizeof headerFileSize);
#if defined(RESTORATION_PACKAGE_TRUST_PROBE) && RESTORATION_PACKAGE_TRUST_PROBE
        probe_native_trust(packageId,patchId,nativeTrust);
#else
        (void)nativeTrust;
#endif
        if (headerFileSize != static_cast<std::uint32_t>(expectedFileSize)) {
            std::array<char, 256> event{};
            const int length = std::snprintf(event.data(),
                                             event.size(),
                                             "ev=package_trust stage=header_size result=mismatch "
                                             "package=0x%04X patch=%u header=%u expected=%u",
                                             packageId,
                                             patchId,
                                             headerFileSize,
                                             static_cast<std::uint32_t>(expectedFileSize));
            if (length > 0) {
                core::log::write(core::log::Channel::client,
                                 core::log::Level::error,
                                 std::string_view(event.data(),
                                                  (std::min)(static_cast<std::size_t>(length),
                                                             event.size() - 1)));
            }
        }
    }
    const auto original = reinterpret_cast<ValidateHeader>(g_handle.original);
    return original(
        validationMask, packageGroup, buildSignature, expectedFileSize, localeToken, 1, header);
}

} // namespace

bool install() noexcept {
#if defined(RESTORATION_NATIVE_PACKAGE_TRUST) && RESTORATION_NATIVE_PACKAGE_TRUST
    // a stock-package trial must use every native authentication check with no code patches
    core::log::write(core::log::Channel::client, core::log::Level::info,
                     "ev=package_trust stage=native_validation active_detours=0 patches=0");
    return !has_ownership();
#endif
    if (has_ownership()) {
        return is_installed();
    }
    std::byte* const target =
        scan_main_image_unique(kValidatorSignature, "package_header_validator");
    std::byte* const extendedHeaderFailure =
        scan_main_image_unique(kExtendedHeaderFailure, "package_extended_header_failure");
    std::byte* const cachedDataHashGate =
        scan_main_image_unique(kCachedDataHashGate, "package_cached_data_hash_gate");
    if (target == nullptr || extendedHeaderFailure == nullptr || cachedDataHashGate == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=package_trust stage=resolve result=fail");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&validate_header)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=package_trust stage=attach result=fail");
        return false;
    }
    if (!kNativeExtendedHash) {
        g_extendedHeaderResult = extendedHeaderFailure + kResultImmediateOffset;
        std::memcpy(
            g_extendedHeaderOriginal.data(), g_extendedHeaderResult, g_extendedHeaderOriginal.size());
    }
    if (!kNativeExtendedHash && !write_code(g_extendedHeaderResult, kSuccessResult,
            g_extendedHeaderProtection, g_extendedHeaderProtectionSaved)) {
        (void)uninstall();
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=package_trust stage=extended_header result=fail");
        return false;
    }
    if (!kNativeCachedHash) {
        g_cachedDataBranch = cachedDataHashGate + kCachedDataBranchOffset;
        std::memcpy(
            g_cachedDataBranchOriginal.data(), g_cachedDataBranch, g_cachedDataBranchOriginal.size());
    }
    if (!kNativeCachedHash && !write_code(g_cachedDataBranch, kAlwaysTakeSuccessBranch,
            g_cachedDataProtection, g_cachedDataProtectionSaved)) {
        (void)uninstall();
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=package_trust stage=cached_data result=fail");
        return false;
    }
    g_installComplete = true;
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=package_trust stage=attach result=ok mode=package_integrity_bypass");
    if (kNativeExtendedHash)
        core::log::write(core::log::Channel::client,core::log::Level::info,
            "ev=package_trust stage=extended_hash result=native patches=0");
    if (kNativeCachedHash)
        core::log::write(core::log::Channel::client,core::log::Level::info,
            "ev=package_trust stage=cached_hash result=native patches=0");
    return true;
}

bool uninstall() noexcept {
    g_installComplete = false;
    bool restored = true;
    if (g_cachedDataBranch != nullptr) {
        const bool cachedDataRestored = write_code(g_cachedDataBranch, g_cachedDataBranchOriginal,
            g_cachedDataProtection, g_cachedDataProtectionSaved);
        restored = restored && cachedDataRestored;
        if (cachedDataRestored) {
            g_cachedDataBranch = nullptr;
            g_cachedDataProtectionSaved = false;
        }
    }
    if (g_extendedHeaderResult != nullptr) {
        const bool extendedHeaderRestored =
            write_code(g_extendedHeaderResult, g_extendedHeaderOriginal,
                g_extendedHeaderProtection, g_extendedHeaderProtectionSaved);
        restored = restored && extendedHeaderRestored;
        if (extendedHeaderRestored) {
            g_extendedHeaderResult = nullptr;
            g_extendedHeaderProtectionSaved = false;
        }
    }
    // keep the header and trampoline owned while the byte patch or protection is uncertain;
    // a failed detach retains its handle for another teardown
    if (!restored) return false;
    return !g_handle.attached || hooking::detour::uninstall(g_handle);
}

bool is_installed() noexcept {
    return g_installComplete && g_handle.attached && (kNativeExtendedHash || g_extendedHeaderResult != nullptr)
           && (kNativeCachedHash || g_cachedDataBranch != nullptr);
}

bool has_ownership() noexcept {
    return g_handle.attached || g_extendedHeaderResult != nullptr || g_cachedDataBranch != nullptr;
}

} // namespace dawn::client::hooks::package_trust
