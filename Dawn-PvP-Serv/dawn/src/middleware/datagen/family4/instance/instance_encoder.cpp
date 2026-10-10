#include "instance_encoder.h"

#include <algorithm>
#include <cstring>

#include "abi.h"
#include "layout.h"

#include <Windows.h>

#include <array>
#include <cstdio>

#include "../../../../core/filesystem/path.h"
#include "../../../../core/logging/log.h"
#include "../../../../core/settings/rule_text.h"
#include "../../../../state/build_data/runtime.h"

namespace dawn::middleware::datagen::family4::instance {
namespace {

[[nodiscard]] bool valid_item_definition(std::uint16_t index, std::uint32_t count) noexcept {
    return count != 0 && count <= layout::kDefinitionIndexCapacity
           && index != layout::kEmptyDefinitionIndex && index < count;
}

[[nodiscard]] bool valid_ordinary_sockets(const OrdinarySockets& sockets,
                                          std::uint32_t itemDefinitionCount) noexcept {
    if (sockets.state == OrdinarySocketBlockState::unresolved) {
        return false;
    }
    if (sockets.state == OrdinarySocketBlockState::absent) {
        return std::none_of(
            sockets.plugs.begin(),
            sockets.plugs.end(),
            [](const std::optional<std::uint16_t>& plug) { return plug.has_value(); });
    }
    if (sockets.state != OrdinarySocketBlockState::present) {
        return false;
    }
    return std::all_of(sockets.plugs.begin(),
                       sockets.plugs.end(),
                       [itemDefinitionCount](const std::optional<std::uint16_t>& plug) {
                           return !plug.has_value()
                                  || valid_item_definition(*plug, itemDefinitionCount);
                       });
}

[[nodiscard]] bool valid_socket_entry_mapping(const ResolvedInstance& input) noexcept {
    if (!input.socketEntryContentsResolved
        || input.socketEntryListIndex == layout::kEmptyDefinitionIndex
        || input.bounds.socketEntryListCount == 0
        || input.bounds.socketEntryListCount > layout::kDefinitionIndexCapacity
        || input.socketEntryListIndex >= input.bounds.socketEntryListCount
        || input.socketEntryCount > layout::kSocketEntryStateCapacity) {
        return false;
    }

    for (std::size_t index = 0; index < input.socketEntryStates.size(); ++index) {
        const SocketEntryState state = input.socketEntryStates[index];
        if (state != SocketEntryState::absent && state != SocketEntryState::ready
            && state != SocketEntryState::active) {
            return false;
        }
        if (index >= input.socketEntryCount && state != SocketEntryState::absent) {
            return false;
        }
    }
    // a selector may only name an entry the list actually has
    for (const SocketSelector& selector : input.socketSelectors) {
        if (selector.entry != layout::kEmptySelectorByte
            && selector.entry >= input.socketEntryCount) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool valid(const ResolvedInstance& input) noexcept {
    return input.instanceSoid != 0 && input.level >= layout::kMinimumItemLevel
           && input.curveSelector == layout::kInitialLevelCurveX
           && input.capSelector == layout::kInitialLevelCapRow
           && valid_item_definition(input.baseDefinitionIndex, input.bounds.itemDefinitionCount)
           && valid_ordinary_sockets(input.ordinarySockets, input.bounds.itemDefinitionCount)
           && valid_socket_entry_mapping(input);
}

// experimental per-item socket overrides from Restoration\socket_field_overrides.txt, rows are
// <item hash hex> <kind> ...; kind 1 aux hashes, kind 2 selector lane, kind 3 random roll bytes
struct FieldOverride {
    std::uint32_t itemHash{};
    std::int32_t kind{};
    std::array<std::uint32_t, 8> values{};
};

constexpr std::size_t kFieldOverrideCapacity = 128;
SRWLOCK g_fieldLock{SRWLOCK_INIT};
bool g_fieldAttempted{};
std::size_t g_fieldCount{};
std::array<FieldOverride, kFieldOverrideCapacity> g_fields{};

void load_field_overrides() noexcept {
    std::array<char, 16384> text{};
    if (core::path::read_artifact_text(L"socket_field_overrides.txt", text)) {
        core::rule_text::Cursor cursor{text.data()};
        while (cursor.seek_field() && g_fieldCount < g_fields.size()) {
            FieldOverride row{};
            row.itemHash = cursor.read_hex();
            row.kind = cursor.read_decimal();
            if (row.kind == 1) {
                row.values[0] = static_cast<std::uint32_t>(cursor.read_decimal());
                row.values[1] = cursor.read_hex();
                row.values[2] = cursor.read_hex();
            } else if (row.kind == 2) {
                for (std::size_t field = 0; field < 4; ++field) {
                    row.values[field] = static_cast<std::uint32_t>(cursor.read_decimal());
                }
            } else if (row.kind == 3) {
                for (std::uint32_t& value : row.values) {
                    value = static_cast<std::uint32_t>(cursor.read_decimal());
                }
            } else {
                break;
            }
            g_fields[g_fieldCount++] = row;
        }
    }
    std::array<char, 80> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=instance stage=socket_field_overrides rows=%zu", g_fieldCount);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

void apply_field_overrides(std::uint16_t baseDefinitionIndex, layout::Object& object) noexcept {
    AcquireSRWLockExclusive(&g_fieldLock);
    if (!g_fieldAttempted) {
        load_field_overrides();
        g_fieldAttempted = true;
    }
    ReleaseSRWLockExclusive(&g_fieldLock);
    state::build_data::items::Definition item{};
    if (g_fieldCount == 0
        || !state::build_data::find_item_definition_index(baseDefinitionIndex, item)) {
        return;
    }
    std::size_t matched = 0;
    for (std::size_t index = 0; index < g_fieldCount; ++index) {
        const FieldOverride& row = g_fields[index];
        if (row.itemHash != item.definitionHash) {
            continue;
        }
        ++matched;
        if (row.kind == 1 && row.values[0] < object.ordinarySockets.sockets.size()) {
            object.ordinarySockets.sockets[row.values[0]].auxiliaryHashes = {row.values[1], row.values[2]};
        } else if (row.kind == 2 && row.values[0] < object.roll.selectors.size()) {
            object.roll.selectors[row.values[0]] =
                layout::SocketSelector{static_cast<std::uint8_t>(row.values[1]),
                                       static_cast<std::uint8_t>(row.values[2]),
                                       static_cast<std::uint8_t>(row.values[3])};
        } else if (row.kind == 3) {
            for (std::size_t byte = 0; byte < object.roll.randomRoll.size(); ++byte) {
                object.roll.randomRoll[byte] = static_cast<std::uint8_t>(row.values[byte]);
            }
        }
    }
    static std::size_t logged = 0;
    if (matched != 0 && logged < 32) {
        ++logged;
        std::array<char, 120> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=instance stage=socket_field_apply item=%08X index=%u rows=%zu",
                                          item.definitionHash,
                                          static_cast<unsigned>(baseDefinitionIndex),
                                          matched);
        if (written > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

void initialize_empty_fields(layout::Object& object) noexcept {
    for (layout::OrdinarySocket& socket : object.ordinarySockets.sockets) {
        socket.plugDefinitionIndex = layout::kEmptyDefinitionIndex;
        socket.auxiliaryHashes.fill(layout::kSafeSocketAuxiliaryHash);
    }
    object.roll.progressionDefinitionIndex = layout::kEmptyDefinitionIndex;
    for (layout::SocketSelector& selector : object.roll.selectors) {
        selector.entry = layout::kEmptySelectorByte;
        selector.group = layout::kEmptySelectorByte;
        selector.element = layout::kEmptySelectorByte;
    }
    object.creation.primaryDefinitionIndex = layout::kEmptyDefinitionIndex;
    object.creation.definitionHashes.fill(layout::kNoDefinitionHash);
    object.creation.secondaryDefinitionIndex = layout::kEmptyDefinitionIndex;
    for (layout::CreationEntry& entry : object.creation.entries) {
        entry.definitionIndices.fill(layout::kEmptyDefinitionIndex);
    }
    object.tailDefinitionIndex = layout::kEmptyDefinitionIndex;
}

} // namespace

bool encode(const ResolvedInstance& input, std::span<std::byte> output) noexcept {
    if (!valid(input) || output.size() < layout::kObjectSize) {
        return false;
    }

    layout::Object object{};
    initialize_empty_fields(object);
    object.instanceSoid = input.instanceSoid;
    object.level.level = input.level;
    object.level.curveX = input.curveSelector;
    object.level.capRow = input.capSelector;
    object.baseDefinitionIndex = input.baseDefinitionIndex;
    object.ordinarySockets.gateMask = layout::kAllSocketBits;
    object.roll.progress = layout::kInitialInstanceProgress;
    object.roll.socketEntryListIndex = input.socketEntryListIndex;

    if (input.ordinarySockets.state == OrdinarySocketBlockState::present) {
        // both permit masks are filled: the plug walk reads the definition's declared plugs as well as
        // this instance's lanes, and skips whichever source its mask leaves unset
        object.ordinarySockets.activeMask = layout::kAllSocketBits;
        object.ordinarySockets.definitionUnlockMask = layout::kAllSocketBits;
        for (std::size_t index = 0; index < input.ordinarySockets.plugs.size(); ++index) {
            const std::optional<std::uint16_t>& plug = input.ordinarySockets.plugs[index];
            if (plug.has_value()) {
                object.ordinarySockets.sockets[index].plugDefinitionIndex = *plug;
                // no universally safe constant for the two aux hashes; a wrong one silently blanks the socket's
                // render. the client expects the socket's own plug hash, falling back to kSafeSocketAuxiliaryHash
                state::build_data::items::Definition plugDefinition{};
                if (state::build_data::find_item_definition_index(*plug, plugDefinition)) {
                    object.ordinarySockets.sockets[index].auxiliaryHashes.fill(
                        plugDefinition.definitionHash);
                }
            }
        }
    }
    std::transform(input.socketEntryStates.begin(),
                   input.socketEntryStates.end(),
                   object.roll.socketEntryStates.begin(),
                   [](SocketEntryState state) { return static_cast<std::uint8_t>(state); });
    // a selection overwrites only the buckets it publishes; the rest keep the empty sentinel
    for (std::size_t bucket = 0; bucket < object.roll.selectors.size(); ++bucket) {
        const SocketSelector& source = input.socketSelectors[bucket];
        if (source.entry == layout::kEmptySelectorByte) {
            continue;
        }
        object.roll.selectors[bucket] =
            layout::SocketSelector{source.entry, source.group, source.element};
    }

    apply_field_overrides(input.baseDefinitionIndex, object);

    std::fill(output.begin(), output.end(), std::byte{});
    std::memcpy(output.data(), &object, sizeof object);
    return true;
}

} // namespace dawn::middleware::datagen::family4::instance
