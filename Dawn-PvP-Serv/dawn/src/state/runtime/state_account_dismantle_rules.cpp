// payouts from Restoration\dismantle_rules.txt: kind 1 rarity base, kind 2 item rarity,
// kind 3 masterwork plug refund. without the file the account's flat policy applies

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "../../core/settings/rule_text.h"
#include "../build_data/runtime.h"
#include "runtime.h"
#include "state_account_transaction_helpers.h"

namespace dawn::state::runtime::detail {
namespace {

namespace authored_inventory = account::inventory;

constexpr std::size_t kRulesTextCapacity = 262144;
constexpr std::size_t kTierRowCapacity = 8192;
constexpr std::size_t kRefundRowCapacity = 4096;
constexpr std::size_t kPayoutRowCapacity = 32;
constexpr std::uint8_t kLowestTier = 2;
constexpr std::uint8_t kHighestTier = 6;

enum RuleKind : std::int32_t {
    payoutRule = 1,
    tierRule = 2,
    refundRule = 3,
};

struct TierRow {
    std::uint32_t itemHash{};
    std::uint8_t tier{};
};

struct MaterialRow {
    std::uint32_t key{};
    std::uint32_t materialHash{};
    std::int32_t quantity{};
};

struct Rules {
    bool loaded{};
    std::size_t tierCount{};
    std::size_t refundCount{};
    std::size_t payoutCount{};
    std::array<TierRow, kTierRowCapacity> tiers{};
    std::array<MaterialRow, kRefundRowCapacity> refunds{};
    std::array<MaterialRow, kPayoutRowCapacity> payouts{};
};

SRWLOCK g_rulesLock{SRWLOCK_INIT};
bool g_rulesAttempted{};
Rules g_rules{};

void load_rules_locked() noexcept {
    static std::array<char, kRulesTextCapacity> text{};
    g_rules = {};
    if (!core::path::read_artifact_text(L"dismantle_rules.txt", text)) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::info,
                         "ev=dismantle stage=rules result=absent fallback=account_policy");
        return;
    }
    bool malformed = false;
    core::rule_text::Cursor cursor{text.data()};
    while (cursor.seek_field()) {
        const std::int32_t kind = cursor.read_decimal();
        if (kind == payoutRule) {
            const std::int32_t tier = cursor.read_decimal();
            const std::uint32_t material = cursor.read_hex();
            const std::int32_t quantity = cursor.read_decimal();
            if (tier < kLowestTier || tier > kHighestTier || quantity <= 0
                || g_rules.payoutCount >= g_rules.payouts.size()) {
                malformed = true;
                continue;
            }
            g_rules.payouts[g_rules.payoutCount++] = {
                static_cast<std::uint32_t>(tier), material, quantity};
        } else if (kind == tierRule) {
            const std::uint32_t item = cursor.read_hex();
            const std::int32_t tier = cursor.read_decimal();
            if (tier < kLowestTier || tier > kHighestTier
                || g_rules.tierCount >= g_rules.tiers.size()) {
                malformed = true;
                continue;
            }
            g_rules.tiers[g_rules.tierCount++] = {item, static_cast<std::uint8_t>(tier)};
        } else if (kind == refundRule) {
            const std::uint32_t plug = cursor.read_hex();
            const std::uint32_t material = cursor.read_hex();
            const std::int32_t quantity = cursor.read_decimal();
            if (quantity <= 0 || g_rules.refundCount >= g_rules.refunds.size()) {
                malformed = true;
                continue;
            }
            g_rules.refunds[g_rules.refundCount++] = {plug, material, quantity};
        } else {
            // an unknown kind cannot be skipped field by field, so the whole file is refused
            malformed = true;
            break;
        }
    }
    std::sort(g_rules.tiers.begin(),
              g_rules.tiers.begin() + static_cast<std::ptrdiff_t>(g_rules.tierCount),
              [](const TierRow& left, const TierRow& right) { return left.itemHash < right.itemHash; });
    std::sort(g_rules.refunds.begin(),
              g_rules.refunds.begin() + static_cast<std::ptrdiff_t>(g_rules.refundCount),
              [](const MaterialRow& left, const MaterialRow& right) { return left.key < right.key; });
    g_rules.loaded = !malformed && g_rules.tierCount != 0 && g_rules.payoutCount != 0;
    std::array<char, 160> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=dismantle stage=rules result=%s tiers=%zu refunds=%zu "
                                    "payouts=%zu",
                                    g_rules.loaded ? "ok" : "malformed",
                                    g_rules.tierCount,
                                    g_rules.refundCount,
                                    g_rules.payoutCount);
    if (count > 0) {
        core::log::write(core::log::Channel::state,
                         g_rules.loaded ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

[[nodiscard]] const Rules& rules() noexcept {
    AcquireSRWLockShared(&g_rulesLock);
    const bool attempted = g_rulesAttempted;
    ReleaseSRWLockShared(&g_rulesLock);
    if (!attempted) {
        AcquireSRWLockExclusive(&g_rulesLock);
        if (!g_rulesAttempted) {
            load_rules_locked();
            g_rulesAttempted = true;
        }
        ReleaseSRWLockExclusive(&g_rulesLock);
    }
    return g_rules;
}

[[nodiscard]] bool add_material(std::uint32_t material,
                                std::int32_t quantity,
                                std::array<DismantleRewardPolicy, kDismantleRewardPolicyCapacity>& output,
                                std::size_t& count) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (output[index].definitionHash == material) {
            output[index].quantity += quantity;
            return true;
        }
    }
    if (count >= output.size()) {
        return false;
    }
    output[count++] = {material, quantity};
    return true;
}

[[nodiscard]] bool add_refund(const Rules& loaded,
                              std::uint32_t plugHash,
                              std::array<DismantleRewardPolicy, kDismantleRewardPolicyCapacity>& output,
                              std::size_t& count) noexcept {
    const auto begin = loaded.refunds.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(loaded.refundCount);
    auto row = std::lower_bound(begin, end, plugHash, [](const MaterialRow& value, std::uint32_t key) {
        return value.key < key;
    });
    for (; row != end && row->key == plugHash; ++row) {
        if (!add_material(row->materialHash, row->quantity, output, count)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool dismantle_rule_payout(const authored_inventory::Item& item,
                           std::array<DismantleRewardPolicy, kDismantleRewardPolicyCapacity>& output,
                           std::size_t& count) noexcept {
    output = {};
    count = 0;
    const Rules& loaded = rules();
    if (!loaded.loaded) {
        return false;
    }
    const auto tierBegin = loaded.tiers.begin();
    const auto tierEnd = tierBegin + static_cast<std::ptrdiff_t>(loaded.tierCount);
    const auto tierRow = std::lower_bound(
        tierBegin, tierEnd, item.definitionHash, [](const TierRow& value, std::uint32_t key) {
            return value.itemHash < key;
        });
    if (tierRow != tierEnd && tierRow->itemHash == item.definitionHash) {
        for (std::size_t index = 0; index < loaded.payoutCount; ++index) {
            const MaterialRow& payout = loaded.payouts[index];
            if (payout.key == tierRow->tier
                && !add_material(payout.materialHash, payout.quantity, output, count)) {
                return true;
            }
        }
    }

    if (item.sockets.policy == authored_inventory::SocketPolicy::authored) {
        for (std::size_t lane = 0; lane < item.sockets.plugCount && lane < item.sockets.plugs.size();
             ++lane) {
            if (item.sockets.plugs[lane].has_value()
                && !add_refund(loaded, *item.sockets.plugs[lane], output, count)) {
                return true;
            }
        }
        return true;
    }
    build_data::items::Definition definition{};
    build_data::items::details::Definition detail{};
    if (!build_data::find_item_definition_hash(item.definitionHash, definition)
        || !build_data::find_configured_item_detail(definition.definitionIndex, detail)) {
        return true;
    }
    for (const std::uint16_t plugIndex : detail.initialPlugIndices) {
        build_data::items::Definition plug{};
        if (build_data::find_item_definition_index(plugIndex, plug)
            && !add_refund(loaded, plug.definitionHash, output, count)) {
            return true;
        }
    }
    return true;
}

} // namespace dawn::state::runtime::detail
