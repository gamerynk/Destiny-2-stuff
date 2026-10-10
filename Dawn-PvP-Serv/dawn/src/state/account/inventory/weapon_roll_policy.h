#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "../../build_data/items/details/item_detail_catalog.h"
#include "../../build_data/items/socket_plugs/socket_plug_catalog.h"

namespace dawn::state::account::inventory::weapon_roll {
namespace details = build_data::items::details;
namespace sockets = build_data::items::socket_plugs;

// native equipment slots 7, 8 and 9 are kinetic, energy and power
[[nodiscard]] inline bool is_weapon(const details::Definition& definition) noexcept {
    return definition.equipmentSlot && *definition.equipmentSlot >= 7
        && *definition.equipmentSlot <= 9
        && definition.instancedDefinitionState == details::InstancedDefinitionState::instanced
        && definition.ordinarySocketCount <= details::kInitialPlugCapacity;
}

class Catalog final {
    static constexpr std::uint32_t kMissing = UINT32_MAX;
    using Lanes = std::array<std::uint32_t, details::kInitialPlugCapacity>;
    std::unordered_map<std::uint16_t, Lanes> lanes_;
    std::unordered_map<std::uint32_t, std::vector<std::uint16_t>> pools_;
public:
    [[nodiscard]] bool build(std::span<const details::Definition> definitions,
                             std::span<const sockets::Rule> rules,
                             std::span<const sockets::Pool> pools,
                             std::span<const sockets::Member> members) {
        lanes_.clear(); pools_.clear();
        for (const auto& definition : definitions) {
            if (!is_weapon(definition)) continue;
            Lanes lanes{}; lanes.fill(kMissing);
            for (std::size_t lane = 0; lane < definition.ordinarySocketCount; ++lane) {
                const auto type = definition.socketTypes[lane];
                const auto key = type != details::kUnavailableSocketType
                    ? static_cast<std::uint32_t>(type)
                    : 0x10000U + static_cast<std::uint32_t>(definition.definitionIndex)
                        * static_cast<std::uint32_t>(details::kInitialPlugCapacity)
                        + static_cast<std::uint32_t>(lane);
                lanes[lane] = key;
                auto& values = pools_[key];
                if (definition.initialPlugIndices[lane] != details::kUnavailableItemIndex)
                    values.push_back(definition.initialPlugIndices[lane]);
            }
            lanes_.emplace(definition.definitionIndex, lanes);
        }
        for (const auto& rule : rules) {
            const auto weapon = lanes_.find(rule.itemDefinitionIndex);
            if (weapon == lanes_.end()) continue;
            if (rule.lane >= details::kInitialPlugCapacity || weapon->second[rule.lane] == kMissing
                || rule.poolIndex >= pools.size()) return clear_invalid();
            const auto& pool = pools[rule.poolIndex];
            if (pool.memberOffset > members.size()
                || pool.memberCount > members.size() - pool.memberOffset) return clear_invalid();
            auto& values = pools_[weapon->second[rule.lane]];
            const auto source = members.subspan(pool.memberOffset, pool.memberCount);
            values.insert(values.end(), source.begin(), source.end());
        }
        for (auto& [key, values] : pools_) {
            (void)key;
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        }
        return !lanes_.empty();
    }

    [[nodiscard]] bool build() {
        std::vector<details::Definition> definitions(details::kDefinitionCapacity);
        std::vector<sockets::Rule> rules(sockets::kRuleCapacity);
        std::vector<sockets::Pool> pools(sockets::kPoolCapacity);
        std::vector<sockets::Member> members(sockets::kMemberCapacity);
        std::size_t definitionCount{}, ruleCount{}, poolCount{}, memberCount{};
        if (!details::snapshot(definitions, definitionCount)
            || !sockets::snapshot(rules, ruleCount, pools, poolCount, members, memberCount)
            || !ruleCount) return clear_invalid();
        return build(std::span(definitions).first(definitionCount),
            std::span(rules).first(ruleCount), std::span(pools).first(poolCount),
            std::span(members).first(memberCount));
    }

    [[nodiscard]] std::span<const std::uint16_t> candidates(std::uint16_t item,
                                                           std::size_t lane) const noexcept {
        const auto weapon = lanes_.find(item);
        if (weapon == lanes_.end() || lane >= details::kInitialPlugCapacity) return {};
        const auto found = pools_.find(weapon->second[lane]);
        return found == pools_.end() ? std::span<const std::uint16_t>{} : std::span(found->second);
    }
    [[nodiscard]] bool allowed(std::uint16_t item, std::size_t lane,
                               std::uint16_t plug) const noexcept {
        const auto values = candidates(item, lane);
        return std::binary_search(values.begin(), values.end(), plug);
    }
private:
    bool clear_invalid() noexcept { lanes_.clear(); pools_.clear(); return false; }
};
} // namespace dawn::state::account::inventory::weapon_roll
