#include "weapon_catalog.h"
#include <Windows.h>
#include <algorithm>
#include <mutex>
#include <map>
#include <thread>
#include <unordered_map>
#include "weapon_strings.h"
#include "../../content/items/packages/internal.h"
#include "../../../state/build_data/items/details/item_detail_catalog.h"
#include "../../../state/build_data/items/socket_plugs/socket_plug_catalog.h"
#include "../../../state/account/inventory/weapon_roll_policy.h"

namespace dawn::client::ui::weapons::catalog {
namespace {
namespace packages = client::content::items::packages;
namespace reader = packages::reader;
namespace tables = packages::tables;
namespace items = state::build_data::items;
std::mutex g_mutex;
std::shared_ptr<const View> g_view = std::make_shared<View>();
std::thread g_worker;
struct Scope {
    reader::BlockKeys keys{};
    std::unique_ptr<reader::Scratch> scratch = std::make_unique<reader::Scratch>();
    ~Scope() { reader::close_files(*scratch); SecureZeroMemory(&keys, sizeof keys); }
};
bool build(View& out) {
    Scope scope;
    core::path::Buffer directory{};
    if (!packages::package_directory(directory) || !packages::collect_keys(scope.keys)) {
        out.error = "Game packages are still loading. Try again after selecting a character."; return false;
    }
    reader::Source source{directory.chars.data(), &scope.keys};
    std::array<std::uint32_t, packages::kContainerCandidates> tags{};
    std::size_t count{};
    std::vector<std::byte> globals, stringMap, localizedIndex;
    tables::Array stringRows{}, localizedRows{};
    bool located = false;
    if (packages::investment_globals_tags(tags, count)) for (std::size_t i = 0; i < count && !located; ++i) {
        std::uint32_t stringTag{}, localizedTag{};
        located = reader::read_tag(source, *scope.scratch, tags[i], globals)
            && tables::child_tag(globals, 33, stringTag) && tables::child_tag(globals, 72, localizedTag)
            && reader::read_tag(source, *scope.scratch, stringTag, stringMap)
            && reader::read_tag(source, *scope.scratch, localizedTag, localizedIndex)
            && tables::find_array_at(stringMap, 8, stringRows) && stringRows.elementClass == 0x80805CDFU
            && tables::find_array_at(localizedIndex, 8, localizedRows)
            && localizedRows.dataOffset <= localizedIndex.size()
            && localizedRows.count <= (localizedIndex.size() - localizedRows.dataOffset) / 8;
    }
    if (!located) { out.error = "Could not read the installed weapon names."; return false; }
    std::unordered_map<std::uint32_t, std::uint32_t> nameTags;
    for (std::size_t i = 0; i < stringRows.count; ++i) {
        tables::IndexRow row{};
        if (tables::index_row(stringMap, stringRows, i, row)) nameTags[row.definitionHash] = row.targetTag;
    }
    std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::string>> banks;
    const auto resolve = [&](std::span<const std::byte> data, std::size_t at) -> std::string {
        std::uint32_t table{}, hash{};
        if (!strings::read(data, at, table) || !strings::read(data, at + 4, hash) || table >= localizedRows.count) return {};
        auto it = banks.find(table);
        if (it == banks.end()) {
            std::unordered_map<std::uint32_t, std::string> values;
            std::uint32_t headerTag{}, dataTag{};
            std::vector<std::byte> header, bytes;
            if (strings::read(std::span<const std::byte>(localizedIndex), localizedRows.dataOffset + table * 8 + 4, headerTag)
                && reader::read_tag(source, *scope.scratch, headerTag, header)
                && strings::read(std::span<const std::byte>(header), 24, dataTag)
                && reader::read_tag(source, *scope.scratch, dataTag, bytes)) (void)strings::decode(header, bytes, values);
            it = banks.emplace(table, std::move(values)).first;
        }
        const auto value = it->second.find(hash);
        return value == it->second.end() ? std::string{} : value->second;
    };
    std::vector<items::Definition> definitions(items::kDefinitionCapacity);
    std::size_t definitionCount{};
    std::vector<items::details::Definition> details(items::details::kDefinitionCapacity);
    std::size_t detailCount{};
    if (!items::snapshot(definitions, definitionCount) || !items::details::snapshot(details, detailCount) || !detailCount) {
        out.error = "The installed weapon catalog is still loading."; return false;
    }
    definitions.resize(definitionCount); details.resize(detailCount);
    std::vector<items::socket_plugs::Rule> rules(items::socket_plugs::kRuleCapacity);
    std::vector<items::socket_plugs::Pool> pools(items::socket_plugs::kPoolCapacity);
    std::vector<items::socket_plugs::Member> members(items::socket_plugs::kMemberCapacity);
    std::size_t ruleCount{}, poolCount{}, memberCount{};
    if (!items::socket_plugs::snapshot(rules, ruleCount, pools, poolCount, members, memberCount)) {
        out.error = "The installed perk catalog is still loading."; return false;
    }
    rules.resize(ruleCount); pools.resize(poolCount); members.resize(memberCount);
    state::account::inventory::weapon_roll::Catalog rollPolicy;
    if (!rollPolicy.build(details, rules, pools, members)) {
        out.error = "The installed weapon roll catalog is still loading."; return false;
    }
    std::unordered_map<std::uint16_t, Choice> choices;
    // the roll policy returns sorted native indices; intern each distinct set once so weapons
    // sharing shader/mod/perk pools share names as well as native candidates
    std::map<std::vector<std::uint16_t>, std::shared_ptr<const Choices>> candidateSets;
    std::unordered_map<std::uint32_t, std::string> types;
    std::unordered_map<std::uint32_t, std::string> descriptions;
    std::unordered_map<std::uint32_t, bool> namedItems;
    std::vector<std::byte> itemStrings;
    const auto choice = [&](std::uint16_t index) -> Choice {
        if (auto found = choices.find(index); found != choices.end()) return found->second;
        if (index >= definitions.size()) return {};
        const auto hash = definitions[index].definitionHash;
        Choice value{hash, {}};
        if (auto tag = nameTags.find(hash); tag != nameTags.end()
            && reader::read_tag(source, *scope.scratch, tag->second, itemStrings)) {
            value.name = resolve(itemStrings, 0x84);
            types[hash] = resolve(itemStrings, 0x90);
            descriptions[hash] = resolve(itemStrings, 0x98);
        }
        namedItems[hash] = !value.name.empty() && !types[hash].empty();
        if (value.name.empty()) value.name = "Item " + std::to_string(hash);
        choices.emplace(index, value); return value;
    };
    for (const auto& detail : details) {
        if (!state::account::inventory::weapon_roll::is_weapon(detail)) continue;
        const auto named = choice(detail.definitionIndex);
        if (!namedItems[named.hash]) continue;
        Weapon weapon; weapon.hash = named.hash; weapon.name = named.name; weapon.type = types[named.hash];
        weapon.description = descriptions[named.hash];
        weapon.laneCount = static_cast<std::uint8_t>((std::min)(static_cast<std::size_t>(detail.ordinarySocketCount), weapon.lanes.size()));
        for (std::size_t lane = 0; lane < weapon.laneCount; ++lane) {
            const auto initial = detail.initialPlugIndices[lane];
            if (initial != items::details::kUnavailableItemIndex) weapon.defaults[lane] = choice(initial).hash;
        }
        for (std::size_t index = 0; index < weapon.laneCount; ++index) {
            const auto candidates = rollPolicy.candidates(detail.definitionIndex, index);
            std::vector<std::uint16_t> key(candidates.begin(), candidates.end());
            auto found = candidateSets.find(key);
            if (found == candidateSets.end()) {
                auto lane = std::make_shared<Choices>();
                lane->reserve(candidates.size());
                for (const auto plug : candidates) lane->push_back(choice(plug));
                std::sort(lane->begin(), lane->end(), [](const auto& a, const auto& b) { return a.name < b.name; });
                found = candidateSets.emplace(std::move(key), std::move(lane)).first;
            }
            weapon.lanes[index] = found->second;
        }
        out.weapons.push_back(std::move(weapon));
    }
    std::sort(out.weapons.begin(), out.weapons.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    if (out.weapons.empty()) { out.error = "No installed weapons are available yet."; return false; }
    return true;
}
}
void load() noexcept {
    std::lock_guard lock(g_mutex);
    if (g_view->loading || !g_view->weapons.empty()) return;
    try {
        // a completed view is published by the worker's last locked operation, so seeing
        // loading=false under this lock means the prior worker no longer needs it
        if (g_worker.joinable()) g_worker.join();
        auto pending = std::make_shared<View>(); pending->loading = true; g_view = pending;
        g_worker = std::thread([] {
            auto result = std::make_shared<View>();
            try { if (!build(*result)) result->weapons.clear(); }
            catch (...) { result->weapons.clear(); result->error = "Could not load the weapon catalog."; }
            std::lock_guard publish(g_mutex); g_view = std::move(result);
        });
    } catch (...) { auto failed = std::make_shared<View>(); failed->error = "Could not start the weapon catalog."; g_view = failed; }
}
std::shared_ptr<const View> snapshot() noexcept { std::lock_guard lock(g_mutex); return g_view; }
// join outside g_mutex so a pending read can publish its immutable view; renderer
// snapshots keep their own shared ownership
void shutdown() noexcept { if (g_worker.joinable()) g_worker.join(); }
}
