#include "loadout_item_resolver.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "../../../../core/filesystem/path.h"
#include "../../../../core/logging/log.h"
#include "../../../../core/settings/rule_text.h"
#include "../../../../state/build_data/runtime.h"
#include "subclass_socket_selection.h"

namespace dawn::middleware::datagen::family4::loadout {
namespace {

namespace authored_inventory = state::account::inventory;
namespace build_buckets = state::build_data::inventory::buckets;
namespace build_details = state::build_data::items::details;
namespace build_items = state::build_data::items;
namespace build_socket_lists = state::build_data::socket_entry_lists;

// native instanced rows always carry exactly one item
constexpr std::int32_t kInstancedQuantity = 1;

[[nodiscard]] bool resolve_plug(std::uint32_t definitionHash,
                                std::size_t itemDefinitionCount,
                                std::uint16_t& output) noexcept {
    build_items::Definition definition{};
    if (!state::build_data::find_item_definition_hash(definitionHash, definition)
        || static_cast<std::size_t>(definition.definitionIndex) >= itemDefinitionCount) {
        return false;
    }
    output = definition.definitionIndex;
    return true;
}

struct CuratedPlug {
    std::uint32_t itemHash{};
    std::uint8_t socket{};
    std::uint32_t plugHash{};
};

constexpr std::size_t kCuratedPlugCapacity = 256;

struct CuratedRolls {
    std::size_t count{};
    std::array<CuratedPlug, kCuratedPlugCapacity> rows{};
};

SRWLOCK g_curatedLock{SRWLOCK_INIT};
bool g_curatedAttempted{};
CuratedRolls g_curated{};

// reads Restoration\curated_rolls.txt once: <hex item hash> <socket index> <hex plug hash>
[[nodiscard]] const CuratedRolls& curated_rolls() noexcept {
    AcquireSRWLockShared(&g_curatedLock);
    const bool attempted = g_curatedAttempted;
    ReleaseSRWLockShared(&g_curatedLock);
    if (attempted) {
        return g_curated;
    }
    AcquireSRWLockExclusive(&g_curatedLock);
    if (!g_curatedAttempted) {
        std::array<char, 16384> text{};
        g_curated = {};
        if (core::path::read_artifact_text(L"curated_rolls.txt", text)) {
            core::rule_text::Cursor cursor{text.data()};
            while (cursor.seek_field() && g_curated.count < g_curated.rows.size()) {
                const std::uint32_t item = cursor.read_hex();
                const std::int32_t socket = cursor.read_decimal();
                const std::uint32_t plug = cursor.read_hex();
                if (socket < 0 || socket > 255) {
                    continue;
                }
                g_curated.rows[g_curated.count++] = {item, static_cast<std::uint8_t>(socket), plug};
            }
        }
        std::array<char, 80> line{};
        const int written = std::snprintf(
            line.data(), line.size(), "ev=loadout stage=curated_rolls rows=%zu", g_curated.count);
        if (written > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
        g_curatedAttempted = true;
    }
    ReleaseSRWLockExclusive(&g_curatedLock);
    return g_curated;
}

[[nodiscard]] bool resolve_ordinary_sockets(const authored_inventory::Sockets& authored,
                                            const build_details::Definition& definition,
                                            std::size_t itemDefinitionCount,
                                            instance::OrdinarySockets& output,
                                            bool explicitPickerRoll = false,
                                            const CuratedRolls* fixture = nullptr) noexcept {
    output = {};
    if (definition.ordinarySocketState == build_details::OrdinarySocketState::absent) {
        if (authored.policy == authored_inventory::SocketPolicy::authored
            && authored.plugCount != 0) {
            return false;
        }
        output.state = instance::OrdinarySocketBlockState::absent;
        return true;
    }
    if (definition.ordinarySocketState != build_details::OrdinarySocketState::present) {
        return false;
    }
    output.state = instance::OrdinarySocketBlockState::present;

    if (authored.policy == authored_inventory::SocketPolicy::nativeDefaults) {
        for (std::size_t index = 0; index < definition.initialPlugIndices.size(); ++index) {
            const std::uint16_t plugIndex = definition.initialPlugIndices[index];
            if (plugIndex == build_details::kUnavailableItemIndex) {
                continue;
            }
            if (static_cast<std::size_t>(plugIndex) >= itemDefinitionCount) {
                return false;
            }
            output.plugs[index] = plugIndex;
        }
        // a curated roll replaces native defaults on every copy that still uses them
        const CuratedRolls& curated = fixture ? *fixture : curated_rolls();
        for (std::size_t row = 0; row < curated.count; ++row) {
            const CuratedPlug& plug = curated.rows[row];
            std::uint16_t plugIndex = 0;
            if (plug.itemHash == definition.definitionHash && plug.socket < output.plugs.size()
                && resolve_plug(plug.plugHash, itemDefinitionCount, plugIndex)) {
                output.plugs[plug.socket] = plugIndex;
            }
        }
        return true;
    }
    if (authored.policy != authored_inventory::SocketPolicy::authored
        || authored.plugCount != definition.ordinarySocketCount) {
        return false;
    }
    for (std::size_t index = 0; index < authored.plugCount; ++index) {
        const std::optional<std::uint32_t>& definitionHash = authored.plugs[index];
        if (!definitionHash.has_value()) {
            continue;
        }
        std::uint16_t plugIndex = 0;
        if (!resolve_plug(*definitionHash, itemDefinitionCount, plugIndex)) {
            return false;
        }
        output.plugs[index] = plugIndex;
    }
    // picker instances persist a complete explicit roll, including native defaults and empty lanes
    if (explicitPickerRoll) return true;
    // changing any socket (shader, sights) saves every lane, untouched ones as native defaults.
    // a curated plug keeps its lane while that lane still holds the default
    const CuratedRolls& curated = fixture ? *fixture : curated_rolls();
    for (std::size_t row = 0; row < curated.count; ++row) {
        const CuratedPlug& plug = curated.rows[row];
        std::uint16_t plugIndex = 0;
        if (plug.itemHash == definition.definitionHash && plug.socket < output.plugs.size()
            && plug.socket < definition.initialPlugIndices.size()
            // an empty native lane (Dire Promise's masterwork socket) is saved empty, which is its default
            // too, so the curated masterwork survives a shader change
            && (output.plugs[plug.socket].has_value()
                    ? *output.plugs[plug.socket] == definition.initialPlugIndices[plug.socket]
                    : definition.initialPlugIndices[plug.socket] == build_details::kUnavailableItemIndex)
            && resolve_plug(plug.plugHash, itemDefinitionCount, plugIndex)) {
            output.plugs[plug.socket] = plugIndex;
        }
    }
    return true;
}

[[nodiscard]] bool resolve_quantity(const authored_inventory::Item& authored,
                                    const build_details::Definition& definition,
                                    std::int32_t& output) noexcept {
    switch (definition.instancedDefinitionState) {
    case build_details::InstancedDefinitionState::stackable:
        output = (std::min)(authored.quantity, definition.maxStackSize);
        return output > 0;
    case build_details::InstancedDefinitionState::instanced:
        output = kInstancedQuantity;
        return true;
    default:
        return false;
    }
}

} // namespace

bool weapon_picker_curated_roll_self_test(std::uint32_t defaultHash,
                                          std::uint32_t replacementHash) noexcept {
    const auto bounds = state::build_data::item_definition_count();
    std::uint16_t defaultIndex{}, replacementIndex{};
    if (!resolve_plug(defaultHash, bounds, defaultIndex)
        || !resolve_plug(replacementHash, bounds, replacementIndex)
        || defaultIndex == replacementIndex) return false;
    build_details::Definition detail{};
    detail.definitionHash = 12345;
    detail.ordinarySocketState = build_details::OrdinarySocketState::present;
    detail.ordinarySocketCount = 2;
    detail.initialPlugIndices[0] = defaultIndex;
    CuratedRolls fixture{};
    fixture.count = 2;
    fixture.rows[0] = {detail.definitionHash, 0, replacementHash};
    fixture.rows[1] = {detail.definitionHash, 1, replacementHash};
    authored_inventory::Sockets authored{};
    authored.policy = authored_inventory::SocketPolicy::authored;
    authored.plugCount = 2;
    authored.plugs[0] = defaultHash;
    instance::OrdinarySockets picker{}, legacy{}, native{};
    if (!resolve_ordinary_sockets(authored, detail, bounds, picker, true, &fixture)
        || !resolve_ordinary_sockets(authored, detail, bounds, legacy, false, &fixture)) return false;
    authored = {};
    if (!resolve_ordinary_sockets(authored, detail, bounds, native, true, &fixture)) return false;
    return picker.plugs[0] == defaultIndex && !picker.plugs[1]
        && legacy.plugs[0] == replacementIndex && legacy.plugs[1] == replacementIndex
        && native.plugs[0] == replacementIndex && native.plugs[1] == replacementIndex
        && authored_inventory::is_weapon_picker_instance(authored_inventory::kWeaponPickerInstancePrefix | 1)
        && !authored_inventory::is_weapon_picker_instance(0x4DA0000000000001ULL);
}

void report_emote_collection_drop(const authored_inventory::Item& authored,
                                  std::size_t itemDefinitionCount,
                                  std::size_t socketEntryListCount) noexcept {
    build_items::Definition itemDefinition{};
    build_details::Definition itemDetail{};
    build_buckets::Descriptor bucket{};
    build_socket_lists::Definition socketList{};
    std::uint8_t nativeEquipmentSlot = 0;
    const char* reason = "unknown";
    if (!state::build_data::find_item_definition_hash(authored.definitionHash, itemDefinition)) {
        reason = "no_item_definition";
    } else if (!state::build_data::find_configured_item_detail(itemDefinition.definitionIndex,
                                                               itemDetail)) {
        reason = "no_item_detail";
    } else if (itemDefinition.bucketId != itemDetail.bucketId) {
        reason = "bucket_mismatch";
    } else if (!authored_inventory::resolve_native_equipment_slot(
                   authored.definitionHash, itemDetail.equipmentSlot, nativeEquipmentSlot)) {
        reason = "no_equipment_slot";
    } else if (static_cast<std::size_t>(nativeEquipmentSlot)
               >= build_details::kEquipmentSlotCount) {
        reason = "equipment_slot_range";
    } else if (!state::build_data::find_inventory_bucket_descriptor(itemDetail.bucketId, bucket)) {
        reason = "no_bucket_descriptor";
    } else if (bucket.arraySelector != build_buckets::ArraySelector::character) {
        reason = "bucket_not_character";
    } else if (!state::build_data::find_socket_entry_list(itemDetail.socketEntryListIndex,
                                                          socketList)) {
        reason = "no_socket_entry_list";
    } else if (static_cast<std::size_t>(itemDefinition.definitionIndex) >= itemDefinitionCount) {
        reason = "item_index_range";
    } else if (static_cast<std::size_t>(socketList.definitionIndex) >= socketEntryListCount) {
        reason = "socket_list_range";
    } else if (socketList.definitionIndex != itemDetail.socketEntryListIndex) {
        reason = "socket_list_mismatch";
    }
    std::array<char, 224> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=emote_collection stage=loadout result=drop reason=%s hash=0x%08X item_index=%u "
        "bucket=%u array_selector=%d slot=%u socket_list=%u",
        reason,
        authored.definitionHash,
        static_cast<unsigned>(itemDefinition.definitionIndex),
        static_cast<unsigned>(itemDetail.bucketId),
        static_cast<int>(bucket.arraySelector),
        static_cast<unsigned>(nativeEquipmentSlot),
        static_cast<unsigned>(itemDetail.socketEntryListIndex));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

constexpr std::uint32_t kCuratedItemReportLimit = 64;

static void report_curated_item(const authored_inventory::Item& authored,
                                const build_details::Definition& detail,
                                const instance::OrdinarySockets& sockets) noexcept {
    static std::atomic<std::uint32_t> reports{0};
    const CuratedRolls& curated = curated_rolls();
    bool matched = false;
    for (std::size_t row = 0; row < curated.count && !matched; ++row) {
        matched = curated.rows[row].itemHash == detail.definitionHash;
    }
    if (!matched || reports.fetch_add(1, std::memory_order_relaxed) >= kCuratedItemReportLimit) {
        return;
    }
    std::array<char, 240> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=loadout stage=curated_item item=%08X soid=%llX policy=%u "
                                "socket_count=%u authored_plugs=%zu lanes=",
                                authored.definitionHash,
                                static_cast<unsigned long long>(authored.instanceSoid),
                                static_cast<unsigned>(authored.sockets.policy),
                                static_cast<unsigned>(detail.ordinarySocketCount),
                                static_cast<std::size_t>(authored.sockets.plugCount));
    for (std::size_t lane = 0; lane < sockets.plugs.size() && written > 0
                               && static_cast<std::size_t>(written) < line.size() - 8;
         ++lane) {
        const int added = std::snprintf(line.data() + written,
                                        line.size() - static_cast<std::size_t>(written),
                                        sockets.plugs[lane].has_value() ? "%s%u" : "%s-",
                                        lane == 0 ? "" : ",",
                                        sockets.plugs[lane].value_or(0));
        if (added <= 0) {
            break;
        }
        written += added;
    }
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1)});
    }
}

bool resolve_item(const authored_inventory::Item& authored,
                  const state::CharacterState& character,
                  std::size_t itemDefinitionCount,
                  std::size_t socketEntryListCount,
                  Candidate& output) noexcept {
    if (!authored_inventory::valid(authored) || itemDefinitionCount == 0
        || itemDefinitionCount > build_items::kDefinitionCapacity || socketEntryListCount == 0
        || socketEntryListCount > build_socket_lists::kDefinitionCapacity) {
        return false;
    }

    build_items::Definition itemDefinition{};
    build_details::Definition itemDetail{};
    build_buckets::Descriptor bucket{};
    build_socket_lists::Definition socketList{};
    std::uint8_t nativeEquipmentSlot = 0;
    if (!state::build_data::find_item_definition_hash(authored.definitionHash, itemDefinition)
        || !state::build_data::find_configured_item_detail(itemDefinition.definitionIndex,
                                                           itemDetail)
        || itemDefinition.bucketId != itemDetail.bucketId
        || !authored_inventory::resolve_native_equipment_slot(
            authored.definitionHash, itemDetail.equipmentSlot, nativeEquipmentSlot)
        || static_cast<std::size_t>(nativeEquipmentSlot)
               >= build_details::kEquipmentSlotCount
        || !state::build_data::find_inventory_bucket_descriptor(itemDetail.bucketId, bucket)
        || bucket.arraySelector != build_buckets::ArraySelector::character
        || !state::build_data::find_socket_entry_list(itemDetail.socketEntryListIndex, socketList)
        || static_cast<std::size_t>(itemDefinition.definitionIndex) >= itemDefinitionCount
        || static_cast<std::size_t>(socketList.definitionIndex) >= socketEntryListCount
        || socketList.definitionIndex != itemDetail.socketEntryListIndex) {
        if (authored.definitionHash == authored_inventory::kEmoteCollectionDefinitionHash) {
            report_emote_collection_drop(authored, itemDefinitionCount, socketEntryListCount);
        }
        return false;
    }

    Candidate candidate{};
    candidate.bucket = bucket;
    candidate.item.equipmentSlot = nativeEquipmentSlot;
    candidate.item.mutationSerial = authored.mutationSerial;
    candidate.item.flags = authored.flags;
    if (!resolve_quantity(authored, itemDetail, candidate.item.quantity)
        || !resolve_ordinary_sockets(authored.sockets,
                                     itemDetail,
                                     itemDefinitionCount,
                                     candidate.item.instance.ordinarySockets,
                                     authored_inventory::is_weapon_picker_instance(authored.instanceSoid))) {
        return false;
    }
    report_curated_item(authored, itemDetail, candidate.item.instance.ordinarySockets);

    candidate.item.instance.instanceSoid = authored.instanceSoid;
    candidate.item.instance.bounds.itemDefinitionCount =
        static_cast<std::uint32_t>(itemDefinitionCount);
    candidate.item.instance.bounds.socketEntryListCount =
        static_cast<std::uint32_t>(socketEntryListCount);
    candidate.item.instance.baseDefinitionIndex = itemDefinition.definitionIndex;
    candidate.item.instance.level = authored.level;
    candidate.item.instance.curveSelector = instance::layout::kInitialLevelCurveX;
    candidate.item.instance.capSelector = instance::layout::kInitialLevelCapRow;
    candidate.item.instance.socketEntryListIndex = socketList.definitionIndex;
    candidate.item.instance.socketEntryCount = socketList.entryCount;
    candidate.item.instance.socketEntryContentsResolved = true;
    resolve_socket_states(socketList,
                          character,
                          candidate.item.instance.socketEntryStates,
                          candidate.item.instance.socketSelectors);
    output = candidate;
    return true;
}

} // namespace dawn::middleware::datagen::family4::loadout
