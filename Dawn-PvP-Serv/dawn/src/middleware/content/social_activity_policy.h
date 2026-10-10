#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dawn::middleware::content::social_activity_policy {
struct Definition { std::uint32_t hash; std::int16_t index; std::string_view package; };
// PC 86657 activity table 81327CF0: index/hash/name triples for public Social rows.
// Red War lap and the two Nadir missions are typed Social but not public destinations
inline constexpr std::array<Definition,8> kDefinitions{{
    {0xE8ABA41BU,20,"city_tower_social_d2"},
    {0x4E0FA012U,22,"campaign_social_space_d2"},
    {0x5D813A4CU,23,"campaign_social_space_d2"},
    {0x13B3BA49U,24,"campaign_social_space_d2"},
    {0xD873EF56U,25,"campaign_social_space_d2"},
    {0x4CEF34A6U,26,"campaign_social_space_d2"},
    {0x34B43AB2U,44,"tangled_shore_social_space"},
    {0x59905E37U,84,"city_tower_social_d2"}
}};
[[nodiscard]] constexpr const Definition* find(std::uint32_t hash) noexcept {
    for(const auto& definition:kDefinitions) if(definition.hash==hash)return &definition;
    return nullptr;
}
[[nodiscard]] constexpr bool matches(std::uint32_t hash,std::int16_t index,
                                     std::string_view package) noexcept {
    const auto* definition=find(hash);
    return definition && definition->index==index && definition->package==package;
}
}
