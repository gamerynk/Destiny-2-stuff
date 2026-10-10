#include "family3_roster.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "../roster/queuez_roster.h"

namespace dawn::middleware::datagen::family3 {
namespace {

constexpr std::size_t kPrimaryIndexCapacity = 27;
constexpr std::size_t kSecondaryIndexCapacity = 5;
// 7 tail bytes complete the native object stride after the final flag
constexpr std::size_t kTailSize = 7;
constexpr std::uint16_t kEmptyDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();

struct RosterLayout {
    std::uint64_t accountSoid{};
    roster::Block roster{};
    std::array<std::uint16_t, kPrimaryIndexCapacity> primaryIndices{};
    std::array<std::uint16_t, kSecondaryIndexCapacity> secondaryIndices{};
    std::uint8_t flag{};
    std::array<std::byte, kTailSize> tail{};
};

static_assert(sizeof(RosterLayout) == kRosterSize);
static_assert(offsetof(RosterLayout, primaryIndices)
              == sizeof(std::uint64_t) + sizeof(roster::Block));
static_assert(offsetof(RosterLayout, secondaryIndices)
              == offsetof(RosterLayout, primaryIndices)
                     + kPrimaryIndexCapacity * sizeof(std::uint16_t));
static_assert(offsetof(RosterLayout, flag)
              == offsetof(RosterLayout, secondaryIndices)
                     + kSecondaryIndexCapacity * sizeof(std::uint16_t));

} // namespace

bool encode_roster(const state::AccountState& account,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (account.primarySoid == 0 || output.size() < sizeof(RosterLayout)) {
        return false;
    }

    RosterLayout layout;
    layout.accountSoid = account.primarySoid;
    if (!roster::initialize(account, layout.roster)) {
        return false;
    }
    std::fill(layout.primaryIndices.begin(), layout.primaryIndices.end(), kEmptyDefinitionIndex);
    std::fill(
        layout.secondaryIndices.begin(), layout.secondaryIndices.end(), kEmptyDefinitionIndex);
    std::memcpy(output.data(), &layout, sizeof layout);
    written = sizeof layout;
    return true;
}

} // namespace dawn::middleware::datagen::family3
