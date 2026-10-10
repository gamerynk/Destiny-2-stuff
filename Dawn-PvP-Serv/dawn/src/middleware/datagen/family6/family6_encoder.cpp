#include "family6_encoder.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace dawn::middleware::datagen::family6 {
namespace {

// slot one starts with its linked key, then the nested transitory structure at +8
constexpr std::size_t kPartyMembersOffset = 0x08;
constexpr std::size_t kMembershipIdOffset = 0x00;
constexpr std::size_t kEmblemDefinitionOffset = 0x08;
constexpr std::size_t kEmblemVariantOffset = 0x0A;
constexpr std::size_t kDisplayNameOffset = 0x0C;
constexpr std::size_t kStatusOffset = 0x8C;
constexpr std::size_t kCurrentActivityOffset =
    kPartyMembersOffset + kMemberCapacity * kMemberSize;

template <typename Value>
void write(std::span<std::byte> destination,
           std::size_t offset,
           const Value& value) noexcept {
    std::memcpy(destination.data() + offset, &value, sizeof value);
}

[[nodiscard]] bool encode_display_name(
    const std::array<char, kDisplayNameCapacity>& source,
    std::span<std::byte> destination) noexcept {
    if (destination.size() != kDisplayNameCapacity * sizeof(char16_t)) {
        return false;
    }
    const auto terminator = std::find(source.begin(), source.end(), '\0');
    const std::size_t length = static_cast<std::size_t>(terminator - source.begin());
    if (length == 0 || length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    std::array<wchar_t, kDisplayNameCapacity> wide{};
    const int converted = MultiByteToWideChar(CP_UTF8,
                                              MB_ERR_INVALID_CHARS,
                                              source.data(),
                                              static_cast<int>(length),
                                              wide.data(),
                                              static_cast<int>(wide.size() - 1U));
    if (converted <= 0) {
        return false;
    }
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    std::memcpy(destination.data(), wide.data(), static_cast<std::size_t>(converted) * sizeof(wchar_t));
    return true;
}

} // namespace

static_assert(kPartyMembersOffset + kMemberCapacity * kMemberSize == 0xB48);
static_assert(kCurrentActivityOffset + 2U * sizeof(std::uint64_t) + 2U * sizeof(float) + 3U
              <= kPartySize);
static_assert(kDisplayNameOffset + kDisplayNameCapacity * sizeof(char16_t) == kStatusOffset);
static_assert(kStatusOffset + sizeof(std::uint32_t) == kMemberSize);

bool encode_anchor(std::uint64_t accountSoid,
                   std::uint64_t linkedAccountSoid,
                   std::span<std::byte> destination) noexcept {
    if (accountSoid == 0 || linkedAccountSoid == 0 || destination.size() != kAnchorSize) {
        return false;
    }
    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, 0, accountSoid);
    write(destination, sizeof accountSoid, linkedAccountSoid);
    return true;
}

bool encode_party(std::uint64_t linkedAccountSoid,
                  std::span<const Member> members,
                  std::span<std::byte> destination) noexcept {
    if (linkedAccountSoid == 0 || members.empty() || members.size() > kMemberCapacity
        || destination.size() != kPartySize) {
        return false;
    }
    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, 0, linkedAccountSoid);
    for (std::size_t index = 0; index < members.size(); ++index) {
        const Member& member = members[index];
        if (member.accountSoid == 0 || member.status == 0) {
            return false;
        }
        const std::size_t base = kPartyMembersOffset + index * kMemberSize;
        write(destination, base + kMembershipIdOffset, member.accountSoid);
        write(destination, base + kEmblemDefinitionOffset, member.emblemDefinition);
        write(destination, base + kEmblemVariantOffset, member.emblemVariant);
        if (!encode_display_name(
                member.displayName,
                destination.subspan(base + kDisplayNameOffset,
                                    kDisplayNameCapacity * sizeof(char16_t)))) {
            return false;
        }
        write(destination, base + kStatusOffset, member.status);
    }
    // current-activity timestamps, scores, counts and the local compat byte stay zero in orbit;
    // the native consumer trace showed its local comparison value is zero too
    return true;
}

} // namespace dawn::middleware::datagen::family6
