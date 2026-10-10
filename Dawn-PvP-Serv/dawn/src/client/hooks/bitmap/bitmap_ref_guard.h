#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../patterns/image_scan.h"

namespace dawn::client::hooks::bitmap {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

// seven sibling widget classes share its first 16 bytes and differ in the stored-field
// offset, so the signature must reach the +0x128 compare
inline constexpr std::string_view kSetterSignatureText =
    "40 53 48 83 EC ? F2 0F 10 02 48 8B D9 8B 4A 08 "
    "F2 0F 11 44 24 ? 8B 44 24 ? 39 83 28 01 00 00";
inline constexpr auto kSetterSignature =
    signature<signature_length(kSetterSignatureText)>(kSetterSignatureText);

struct BitmapRefWords {
    static constexpr std::size_t tag = 0;
    static constexpr std::size_t reference = 1;
    static constexpr std::size_t state = 2;
    static constexpr std::size_t count = 3;
};

inline constexpr std::uint32_t kNoneReference = 0xFFFFFFFFu;

// tag space is the whole 0x80 and 0x81 top byte; a narrower test blanks real bitmaps
// (0x80A146C7 and neighbours). Zero or a definition hash sits below 0x80
inline constexpr std::uint32_t kTagTopByteLow = 0x80u;
inline constexpr std::uint32_t kTagTopByteHigh = 0x81u;
inline constexpr int kTopByteShift = 24;

inline constexpr unsigned kMaxReports = 8;

inline constexpr std::size_t kReportCapacity = 128;

inline constexpr unsigned kMaxSeenReports = 40;

[[nodiscard]] bool install_bitmap_ref_guard() noexcept;

void uninstall_bitmap_ref_guard() noexcept;

} // namespace dawn::client::hooks::bitmap
