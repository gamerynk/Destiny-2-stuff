#include "hash_name_build.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/named_tags.h"
#include "../../../middleware/content/packages/reader/parallel.h"
#include "../../../state/build_data/runtime.h"
#include "hash_name_matcher.h"

namespace dawn::client::content::hash_names {
namespace {

namespace packages = middleware::content::packages;
namespace reader = middleware::content::packages::reader;

// tag class of named installed objects; their blobs end with the object's own id
constexpr std::uint32_t kNamedObjectClass = 0x80809C36U;

[[nodiscard]] bool collect_tag(void* context, std::uint32_t tag) noexcept {
    return add_tag(*static_cast<Storage*>(context), tag);
}

using Rows = std::array<names_state::Name, names_state::kNameCapacity>;

std::vector<Rows> g_rows{};

struct BatchContext {
    Storage* storage{};
};

[[nodiscard]] bool prepare_rows(const Storage& storage, std::size_t workers) noexcept {
    if (storage.targetCount > names_state::kNameCapacity) {
        return false;
    }
    if (g_rows.size() < workers) {
        g_rows.resize(workers);
    }
    return g_rows.size() >= workers;
}

void release_rows() noexcept {
    g_rows.clear();
    g_rows.shrink_to_fit();
}

void collect_blob(void* context,
                  std::size_t worker,
                  std::uint32_t,
                  std::span<const std::byte> blob) noexcept {
    const Storage& storage = *static_cast<BatchContext*>(context)->storage;
    if (worker >= g_rows.size()) {
        return;
    }
    MatchState state{{storage.targets.data(), storage.targetCount},
                     {g_rows[worker].data(), storage.targetCount}};
    offer_blob(state, blob);
}

[[nodiscard]] bool collect_named(void* context, const packages::named_tags::Entry& entry) noexcept {
    auto& storage = *static_cast<Storage*>(context);
    MatchState state = match_state(storage);
    const std::string_view name(entry.name.data(), entry.nameLength);
    std::size_t start = 0;
    for (std::size_t index = 0; index <= name.size(); ++index) {
        if (index != name.size() && state::build_data::hash_names::name_character(name[index])) {
            continue;
        }
        if (index > start) {
            offer(state, name.substr(start, index - start));
        }
        start = index + 1;
    }
    return true;
}

void report(const Storage& storage, const char* result) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const std::uint64_t elapsed =
        storage.startedTick == 0 ? 0 : GetTickCount64() - storage.startedTick;
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=build_data stage=hash_names hashes=%zu objects=%zu "
                                      "read=%zu named=%zu ms=%llu result=%s",
                                      storage.targetCount,
                                      storage.tagCount,
                                      storage.cursor,
                                      storage.resolvedCount,
                                      static_cast<unsigned long long>(elapsed),
                                      result);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         storage.resolvedCount != 0 ? core::log::Level::info
                                                    : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool publish(Storage& storage, const char* result) noexcept {
    finish(storage);
    const bool published = state::build_data::publish_hash_names(
        std::span(storage.resolved).first(storage.resolvedCount));
    report(storage, published ? result : "publish");
    return published;
}

} // namespace

bool build(const reader::Source& source, reader::Scratch&) noexcept {
    if (state::build_data::hash_names_ready()) {
        return true;
    }
    if (!state::build_data::scenario_layouts_ready() || !state::build_data::spawn_sets_ready()) {
        return false;
    }
    static Storage storage{};
    if (!storage.collected) {
        reset(storage);
        storage.startedTick = GetTickCount64();
        if (!collect_targets(storage)) {
            report(storage, "targets");
            reset(storage);
            return false;
        }
        packages::named_tags::DirectoryResult directory{};
        (void)packages::named_tags::extract_directory(
            source.directory, &collect_named, &storage, directory);
        reader::ScanResult scan{};
        if (!reader::scan_class(source.directory, kNamedObjectClass, &collect_tag, &storage, scan)
            || scan.matches != storage.tagCount) {
            return publish(storage, "sweep");
        }
        storage.collected = true;
        report(storage, "collected");
        return false;
    }

    const std::size_t workers = reader::parallel::worker_count();
    if (!prepare_rows(storage, workers)) {
        return publish(storage, "readers");
    }
    const std::size_t batch = (std::min)(kBatchTags, storage.tagCount - storage.cursor);
    BatchContext context{&storage};
    if (!reader::parallel::read_tags(
            source, {storage.tags.data() + storage.cursor, batch}, &collect_blob, &context)) {
        return publish(storage, "readers");
    }
    for (std::size_t worker = 0; worker < workers; ++worker) {
        merge(storage, {g_rows[worker].data(), storage.targetCount});
    }
    storage.cursor += batch;
    if (storage.cursor < storage.tagCount) {
        return false;
    }
    const bool published = publish(storage, "ok");
    if (published) {
        release_rows();
        reader::parallel::release();
    }
    return published;
}

} // namespace dawn::client::content::hash_names
