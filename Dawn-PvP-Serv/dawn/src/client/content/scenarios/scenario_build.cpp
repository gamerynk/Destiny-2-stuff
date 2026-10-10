#include "scenario_build.h"

#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../state/build_data/runtime.h"
#include "internal.h"

namespace dawn::client::content::scenarios {
namespace {

namespace packages = middleware::content::packages;

void report(const Storage& storage,
            std::size_t kept,
            std::size_t rostered,
            const char* result) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=build_data stage=scenarios tags=%zu named=%zu live=%zu kept=%zu "
                      "groups=%zu dropped=%zu rostered=%zu result=%s",
                      storage.liveTagCount,
                      storage.rowCount,
                      storage.liveRowCount,
                      kept,
                      storage.roster.groupCount,
                      storage.roster.unresolvedGroups,
                      rostered,
                      result);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         kept != 0 ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool walk_rosters(const packages::reader::Source& source,
                                packages::reader::Scratch& scratch,
                                Storage& storage) noexcept {
    const auto rows = std::span(storage.rows).first(storage.keptCount);
    if (!build_rosters(source, scratch, storage.roster, rows)) {
        return false;
    }
    std::size_t rostered = 0;
    for (const layouts::Definition& row : rows) {
        rostered += row.rosterGroupCount != 0 ? 1U : 0U;
    }
    const bool published = state::build_data::publish_scenario_layouts(
        rows, std::span(storage.roster.groups).first(storage.roster.groupCount));
    report(storage, storage.keptCount, rostered, published ? "ok" : "publish");
    storage.roster = {};
    return published;
}

} // namespace

bool build(const packages::reader::Source& source, packages::reader::Scratch& scratch) noexcept {
    if (state::build_data::scenario_layouts_ready()) {
        return true;
    }
    static Storage storage{};
    if (!storage.collected) {
        const char* reason = nullptr;
        if (!collect_rows(source, storage, reason)) {
            report(storage, 0, 0, reason);
            return false;
        }
        storage.collected = true;
        report(storage, 0, 0, "collecting");
        return false;
    }
    if (!storage.compacted) {
        if (!resolve_pending(source, scratch, storage)) {
            return false;
        }
        // decide completeness before compacting, since compact_rows is destructive. A partial pass
        // used to latch and publish a domain missing pvp_shaft, so the client got an empty roster and a black screen
        if (storage.resolvedCount < storage.liveRowCount
            && storage.resolveRounds < kMaxResolveRounds) {
            if (storage.resolveRounds == 0) {
                report(storage, storage.resolvedCount, 0, "partial");
            }
            rearm_resolve(storage);
            return false;
        }
        compact_rows(storage);
        // an empty result is never a finished pass: block keys arrive during boot, so a window that
        // closes first reads nothing. Only the first empty round reports, as retry runs every slice
        if (storage.keptCount == 0) {
            if (storage.resolveRounds == 0) {
                report(storage, 0, 0, "empty");
            }
            rearm_resolve(storage);
            return false;
        }
        report(storage, storage.keptCount, 0, "collected");
        storage.compacted = true;
    }
    return walk_rosters(source, scratch, storage);
}

} // namespace dawn::client::content::scenarios
