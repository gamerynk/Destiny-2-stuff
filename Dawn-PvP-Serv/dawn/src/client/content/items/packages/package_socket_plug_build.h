#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../../../middleware/content/packages/tables/items.h"
#include "../../../../state/build_data/items/item_catalog.h"
#include "../../../../state/build_data/items/socket_plugs/definition.h"

namespace dawn::client::content::items::packages {

namespace tables = middleware::content::packages::tables;
namespace socket_plugs = state::build_data::items::socket_plugs;

class SocketPlugBuild final {
public:
    SocketPlugBuild() = default;
    ~SocketPlugBuild() = default;
    SocketPlugBuild(const SocketPlugBuild&) = delete;
    SocketPlugBuild& operator=(const SocketPlugBuild&) = delete;

    [[nodiscard]] bool
    prepare(std::span<const std::uint8_t> specialCategories,
            std::span<const state::build_data::items::Definition> itemDefinitions) noexcept;

    [[nodiscard]] bool append(const tables::items::Row& item,
                              std::span<const std::byte> itemDefinition,
                              std::span<const std::byte> plugSetTable,
                              std::size_t itemDefinitionCount) noexcept;

    [[nodiscard]] bool publish() noexcept;

    [[nodiscard]] std::size_t skipped() const noexcept;
    [[nodiscard]] std::size_t rule_count() const noexcept;
    [[nodiscard]] std::size_t pool_count() const noexcept;
    [[nodiscard]] std::size_t member_count() const noexcept;

    [[nodiscard]] bool add(std::uint32_t itemDefinitionIndex,
                           std::size_t itemDefinitionCount) noexcept;

private:
    struct PoolLookup {
        std::uint64_t fingerprint{};
        std::uint32_t poolIndex{UINT32_MAX};
    };

    // a plug set names three categories: reusable, randomized, and the socket's own default
    static constexpr std::size_t kCategoryCount = 3;
    static constexpr std::size_t kLookupCapacity = 1U << 19U;

    std::vector<socket_plugs::Rule> rules_{};
    std::vector<socket_plugs::Pool> pools_{};
    std::vector<socket_plugs::Member> members_{};
    std::vector<socket_plugs::Member> candidates_{};
    std::vector<socket_plugs::Member> categoryMembers_{};
    std::vector<PoolLookup> lookup_{};
    std::array<std::size_t, kCategoryCount> categoryCounts_{};
    std::array<socket_plugs::Member, 3> trackerMembers_{};
    std::size_t trackerCount_{};
    std::size_t ruleCount_{};
    std::size_t poolCount_{};
    std::size_t memberCount_{};
    std::size_t candidateCount_{};
    std::size_t skipped_{};

    [[nodiscard]] bool intern(std::uint32_t& poolIndex) noexcept;
    void release() noexcept;
};

[[nodiscard]] std::uint8_t special_plug_category(std::uint32_t categoryHash) noexcept;

} // namespace dawn::client::content::items::packages
