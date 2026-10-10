#include "activity_destination_spawn_binding.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../build_data/runtime.h"

namespace dawn::state::activity::destination {
namespace {

constexpr std::size_t kSpawnRowCapacity = 512;

std::atomic_uint32_t g_reportedHash{};

[[nodiscard]] std::string_view name_of(const DestinationSelection& selection) noexcept {
    return {reinterpret_cast<const char*>(selection.packageName.data()),
            selection.packageNameLength};
}

[[nodiscard]] bool loads_package(const build_data::scenarios::Definition& layout,
                                 const build_data::spawn_sets::NameHash& row) noexcept {
    if (row.inMapPackage != 0) {
        return true;
    }
    const std::size_t declared =
        layout.packageCount < layout.packages.size() ? layout.packageCount : layout.packages.size();
    for (std::size_t index = 0; index < row.activityPackageCount; ++index) {
        for (std::size_t package = 0; package < declared; ++package) {
            if (layout.packages[package] == row.activityPackages[index]) {
                return true;
            }
        }
    }
    return false;
}

void report_dropped(std::string_view name, std::uint32_t hash) noexcept {
    if (g_reportedHash.exchange(hash, std::memory_order_acq_rel) == hash) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=spawn_set result=dropped name=%.*s "
                                      "spawn=0x%08X reason=not_loaded",
                                      static_cast<int>(name.size()),
                                      name.data(),
                                      hash);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

std::uint32_t attachable_spawn_set_hash(const DestinationSelection& selection,
                                        std::uint32_t fallback) noexcept {
    const std::uint32_t hash = resolve_spawn_set_hash(selection, fallback);
    if (hash == 0 || hash == kAbsentSpawnSetHash) {
        return hash;
    }
    const std::string_view name = name_of(selection);
    build_data::scenarios::Definition layout{};
    if (name.empty() || !build_data::find_scenario_layout(name, layout)) {
        return hash;
    }
    const std::string_view stem(layout.spawnStem.data(), layout.spawnStemLength);
    static std::array<build_data::spawn_sets::NameHash, kSpawnRowCapacity> rows{};
    std::size_t count = 0;
    if (stem.empty() || !build_data::find_spawn_sets(stem, rows, count)) {
        return hash;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index].value != hash) {
            continue;
        }
        if (loads_package(layout, rows[index])) {
            g_reportedHash.store(0, std::memory_order_release);
            return hash;
        }
        report_dropped(name, hash);
        return kAbsentSpawnSetHash;
    }
    // a hash no row carries is not proof of a miss, since the row set can be capped; send it as picked
    return hash;
}

} // namespace dawn::state::activity::destination
