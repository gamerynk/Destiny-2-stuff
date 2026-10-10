#include "spawn_set_build.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <span>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/reader/parallel.h"
#include "../../../middleware/content/packages/tables/component_container_reader.h"
#include "../../../middleware/content/packages/tables/spawn_reader.h"
#include "../../../state/build_data/runtime.h"
#include "spawn_set_catalog_builder.h"

namespace dawn::client::content::spawn_sets {
namespace {

namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

struct ReadContext {
    const reader::Source* source{};
    reader::Scratch* scratch{};
    std::vector<std::byte>* bytes{};
    std::vector<reader::parallel::Held>* kept{};
    std::size_t keptCursor{};
};

[[nodiscard]] bool collect(void* context, const reader::ClassEntry& entry) noexcept {
    return add_entry(*static_cast<Storage*>(context), entry);
}

[[nodiscard]] bool collect_component(void* context, std::uint32_t tag) noexcept {
    return add_component(*static_cast<Storage*>(context), tag);
}

[[nodiscard]] bool collect_container(void* context, std::uint32_t tag) noexcept {
    return add_container(*static_cast<Storage*>(context), tag);
}

void prefetch_blobs(void* context, std::span<const std::uint32_t> tags) noexcept {
    auto& read = *static_cast<ReadContext*>(context);
    read.keptCursor = 0;
    if (!reader::parallel::read_kept(*read.source, tags, *read.kept)) {
        read.kept->clear();
    }
}

[[nodiscard]] bool
read_blob(void* context, std::uint32_t tag, std::span<const std::byte>& blob) noexcept {
    auto& read = *static_cast<ReadContext*>(context);
    blob = {};
    if (read.keptCursor < read.kept->size() && (*read.kept)[read.keptCursor].tag == tag) {
        blob = (*read.kept)[read.keptCursor].blob;
        ++read.keptCursor;
        return true;
    }
    if (!reader::read_tag(*read.source, *read.scratch, tag, *read.bytes)) {
        return false;
    }
    blob = *read.bytes;
    return true;
}

void report(const Storage& storage, const char* result) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const std::uint64_t elapsed =
        storage.startedTick == 0 ? 0 : GetTickCount64() - storage.startedTick;
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=build_data stage=spawn_sets tags=%zu stems=%zu "
                                      "hashes=%zu points=%zu dropped=%zu bound=%zu components=%zu "
                                      "containers=%zu read=%zu skipped=%zu ms=%llu result=%s",
                                      storage.entryCount,
                                      storage.stemCount,
                                      storage.nameHashCount,
                                      storage.pointCount,
                                      storage.pointsDropped,
                                      storage.spawnComponentCount,
                                      storage.componentCount,
                                      storage.containerCount,
                                      storage.cursor,
                                      storage.skipped,
                                      static_cast<unsigned long long>(elapsed),
                                      result);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         storage.stemCount != 0 ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

bool build(const reader::Source& source, reader::Scratch& scratch) noexcept {
    if (state::build_data::spawn_sets_ready()) {
        return true;
    }
    static Storage storage{};
    static std::vector<std::byte> bytes{};
    static std::uint64_t readDeadlineTick{};
    if (!storage.collected) {
        reset(storage);
        storage.startedTick = GetTickCount64();
        reader::ScanResult result{};
        // the sweep reads every package header and entry table, so run it once; re-sweeping each retry cost minutes of boot
        reader::ScanResult components{};
        reader::ScanResult containers{};
        if (!reader::scan_class_entries(
                source.directory, tables::kSpawnSetClass, &collect, &storage, result)
            || result.matches != storage.entryCount
            || !reader::scan_class(
                source.directory, tables::kComponentClass, &collect_component, &storage, components)
            || !reader::scan_class(
                source.directory, tables::kContainerClass, &collect_container, &storage, containers)
            || !finish_collection(storage)) {
            report(storage, storage.entryCount == 0 ? "empty" : "sweep");
            reset(storage);
            return state::build_data::publish_spawn_sets({}, {}, {});
        }
        readDeadlineTick = GetTickCount64() + kReadWindowMs;
        report(storage, "collected");
        return false;
    }
    static std::vector<reader::parallel::Held> kept{};
    ReadContext context{&source, &scratch, &bytes, &kept, 0};
    const AdvanceResult result = advance(storage,
                                         &read_blob,
                                         &prefetch_blobs,
                                         &context,
                                         kReadBudget,
                                         GetTickCount64() >= readDeadlineTick);
    if (result == AdvanceResult::invalid) {
        // a retry cannot help, and an empty catalogue still finishes the pipeline
        report(storage, "read");
        reset(storage);
        return state::build_data::publish_spawn_sets({}, {}, {});
    }
    if (result != AdvanceResult::complete) {
        return false;
    }
    const bool published = state::build_data::publish_spawn_sets(
        std::span(storage.stems).first(storage.stemCount),
        std::span(storage.nameHashes).first(storage.nameHashCount),
        std::span(storage.points).first(storage.pointCount));
    report(storage, published ? "ok" : "publish");
    if (published) {
        bytes.clear();
        bytes.shrink_to_fit();
        kept.clear();
        kept.shrink_to_fit();
    }
    return published;
}

} // namespace dawn::client::content::spawn_sets
