#include "ui_hud_status_overlay.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <imgui.h>
#include <string_view>

#include "../../../../client/hooks/bootflow/bootflow_hook_lifecycle.h"
#include "../../../../client/player/player_position.h"
#include "../../../../middleware/content/packages/tables/region_reader.h"
#include "../../../../middleware/content/packages/tables/spawn_reader.h"
#include "../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/build_data/runtime.h"
#include "../overlay.h"

namespace dawn::core::ui::hud::overlays::status {
namespace {

namespace activity = state::activity;
namespace layouts = state::build_data::scenarios;
namespace tables = middleware::content::packages::tables;

constexpr std::size_t kValueCapacity = 96;
constexpr char kWidestLabel[] = "Closest spawn";
constexpr char kOutOfWorld[] = "not in world";
constexpr char kUnknown[] = "unknown";
constexpr char kNoPosition[] = "waiting for a position";
constexpr char kNoLines[] = "every status line is off";
constexpr std::uint64_t kSpawnSearchIntervalMs = 250;

using Value = std::array<char, kValueCapacity>;

struct Status {
    Value activity{};
    Value bubble{};
    Value sliceSet{};
    Value spawn{};
    bool inWorld{};
};

struct SpawnResult {
    Value text{};
    std::uint64_t searchedTick{};
    bool valid{};
};

SpawnResult g_spawn{};

void assign(std::string_view value, Value& output) noexcept {
    output = {};
    (void)std::snprintf(
        output.data(), output.size(), "%.*s", static_cast<int>(value.size()), value.data());
}

[[nodiscard]] std::string_view resolve_name(std::uint32_t hash,
                                            state::build_data::hash_names::Name& storage) noexcept {
    if (!state::build_data::find_hash_name(hash, storage)) {
        return {};
    }
    return {storage.name.data(), storage.nameLength};
}

[[nodiscard]] std::string_view
spawn_set_name(std::uint32_t hash, state::build_data::hash_names::Name& storage) noexcept {
    if (hash == tables::kDefaultSpawnNameHash) {
        return "default";
    }
    if (hash == tables::kUnnamedSpawnNameHash) {
        return "unnamed";
    }
    return resolve_name(hash, storage);
}

[[nodiscard]] std::string_view
name_of(const activity::destination::DestinationSelection& selection) noexcept {
    return {reinterpret_cast<const char*>(selection.packageName.data()),
            selection.packageNameLength};
}

void build_bubble(const layouts::Definition& layout, std::int32_t region, Value& output) noexcept {
    const auto bubble = static_cast<std::size_t>(region) / tables::kSliceSetIndexFactor;
    if (region < 0 || bubble >= layout.bubbleCount) {
        assign(kUnknown, output);
        return;
    }
    state::build_data::hash_names::Name storage{};
    const std::string_view named = resolve_name(layout.bubbleHashes[bubble], storage);
    (void)std::snprintf(output.data(),
                        output.size(),
                        "%zu  0x%08X%s%.*s",
                        bubble,
                        layout.bubbleHashes[bubble],
                        named.empty() ? "" : "  ",
                        static_cast<int>(named.size()),
                        named.data());
}

void build_slice_set(std::int32_t region, Value& output) noexcept {
    if (region < 0) {
        assign(kUnknown, output);
        return;
    }
    const auto index = static_cast<std::uint32_t>(region);
    (void)std::snprintf(
        output.data(), output.size(), "%u  state %u", index, index % tables::kSliceSetIndexFactor);
}

void build_spawn(std::string_view stem, Value& output) noexcept {
    const client::player::position::Snapshot player = client::player::position::snapshot();
    const std::uint64_t now = GetTickCount64();
    if (!player.present || stem.empty()) {
        assign(kNoPosition, output);
        return;
    }
    if (g_spawn.valid && now - g_spawn.searchedTick < kSpawnSearchIntervalMs) {
        output = g_spawn.text;
        return;
    }
    state::build_data::spawn_sets::Point point{};
    float distance = 0.0F;
    g_spawn = {};
    g_spawn.searchedTick = now;
    if (!state::build_data::find_nearest_spawn_point(stem, player.position, point, distance)) {
        assign(kUnknown, output);
        return;
    }
    state::build_data::hash_names::Name storage{};
    const std::string_view named = spawn_set_name(point.nameHash, storage);
    (void)std::snprintf(g_spawn.text.data(),
                        g_spawn.text.size(),
                        "0x%08X%s%.*s  %.1f units",
                        point.nameHash,
                        named.empty() ? "" : "  ",
                        static_cast<int>(named.size()),
                        named.data(),
                        static_cast<double>(distance));
    g_spawn.valid = true;
    output = g_spawn.text;
}

[[nodiscard]] Status read_status() noexcept {
    Status status{};
    const std::uint64_t sessionId =
        activity::membership::live_region_session(activity::kAbsentSessionId);
    // the client's own step is published every frame; the world phase only moves on the
    // spawn gate, which stops being polled once in, so it stays 'arrived' in orbit
    status.inWorld = client::hooks::bootflow::in_world();
    if (!status.inWorld) {
        g_spawn = {};
        client::player::position::reset();
        return status;
    }
    // a paired external server owns its session table in another process, so missing local
    // metadata must not override native world state or retire a valid position
    if (sessionId == activity::kAbsentSessionId) {
        g_spawn = {};
        assign(kUnknown, status.activity);
        assign(kUnknown, status.bubble);
        assign(kUnknown, status.sliceSet);
        assign(kUnknown, status.spawn);
        return status;
    }
    activity::destination::DestinationSelection selection{};
    (void)activity::destination::snapshot(sessionId, selection);
    const std::string_view name = name_of(selection);
    assign(name.empty() ? std::string_view(kUnknown) : name, status.activity);

    const std::int32_t region = activity::membership::reported_region(sessionId);
    layouts::Definition layout{};
    if (!state::build_data::find_scenario_layout(name, layout)) {
        assign(kUnknown, status.bubble);
        assign(kUnknown, status.spawn);
        build_slice_set(region, status.sliceSet);
        return status;
    }
    build_bubble(layout, region, status.bubble);
    build_slice_set(region, status.sliceSet);
    build_spawn({layout.spawnStem.data(), layout.spawnStemLength}, status.spawn);
    return status;
}

void draw_line(StatusLine line, const Value& value, float valueColumn) noexcept {
    if (!enabled(line)) {
        return;
    }
    ImGui::TextDisabled("%s", display_name(line));
    ImGui::SameLine(valueColumn);
    ImGui::TextUnformatted(value.data());
}

} // namespace

void draw() noexcept {
    if (!enabled(StatusLine::activity) && !enabled(StatusLine::bubble)
        && !enabled(StatusLine::sliceSet) && !enabled(StatusLine::closestSpawn)) {
        ImGui::TextDisabled("%s", kNoLines);
        return;
    }
    const Status status = read_status();
    if (!status.inWorld) {
        ImGui::TextDisabled("%s", kOutOfWorld);
        return;
    }
    const float valueColumn =
        ImGui::CalcTextSize(kWidestLabel).x + (ImGui::GetStyle().ItemSpacing.x * 2.0F);
    draw_line(StatusLine::activity, status.activity, valueColumn);
    draw_line(StatusLine::bubble, status.bubble, valueColumn);
    draw_line(StatusLine::sliceSet, status.sliceSet, valueColumn);
    draw_line(StatusLine::closestSpawn, status.spawn, valueColumn);
}

} // namespace dawn::core::ui::hud::overlays::status
