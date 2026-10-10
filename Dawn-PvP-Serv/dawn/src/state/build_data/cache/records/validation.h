#pragma once

#include "domains.h"

namespace dawn::state::build_data::cache::records {

[[nodiscard]] bool canonicalize(MutableDomains domains, const DomainCounts& counts) noexcept;

[[nodiscard]] bool
valid_item_detail_links(std::span<const items::details::Definition> details,
                        std::span<const items::Definition> itemDefinitions,
                        std::span<const inventory::buckets::Descriptor> inventoryBuckets,
                        std::span<const socket_entry_lists::Definition> socketEntryLists) noexcept;

[[nodiscard]] bool
valid_socket_plug_links(std::span<const items::socket_plugs::Rule> rules,
                        std::span<const items::socket_plugs::Pool> pools,
                        std::span<const items::socket_plugs::Member> members,
                        std::span<const items::Definition> itemDefinitions,
                        std::span<const items::details::Definition> details) noexcept;

[[nodiscard]] bool
valid_collectible_links(std::span<const collectibles::Definition> collectibleDefinitions,
                        std::span<const items::Definition> itemDefinitions) noexcept;

[[nodiscard]] bool valid_domains(Domains domains) noexcept;

} // namespace dawn::state::build_data::cache::records
