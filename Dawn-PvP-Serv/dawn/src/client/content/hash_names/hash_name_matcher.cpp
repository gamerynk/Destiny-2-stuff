#include "hash_name_matcher.h"

#include <algorithm>

#include "../../../state/build_data/runtime.h"

namespace dawn::client::content::hash_names {
namespace {

namespace layouts = state::build_data::scenarios;
namespace spawn_state = state::build_data::spawn_sets;

// FNV-1 offset basis; the engine hashes names with FNV-1, not FNV-1a
constexpr std::uint32_t kHashBasis = 0x811C9DC5U;
constexpr std::uint32_t kHashPrime = 0x01000193U;

[[nodiscard]] std::uint32_t hash_of(std::string_view text) noexcept {
    std::uint32_t value = kHashBasis;
    for (const char character : text) {
        value = (value * kHashPrime) ^ static_cast<std::uint8_t>(character);
    }
    return value;
}

void keep(MatchState& state, std::string_view text, std::size_t slot) noexcept {
    names_state::Name& row = state.resolved[slot];
    // shortest candidate is the bare name; a longer one is a suffix of some object's name
    if (row.nameLength != 0 && row.nameLength <= text.size()) {
        return;
    }
    row = {};
    row.hash = state.targets[slot];
    std::copy_n(text.begin(), text.size(), row.name.begin());
    row.nameLength = static_cast<std::uint8_t>(text.size());
}

[[nodiscard]] bool
target_slot(const MatchState& state, std::uint32_t hash, std::size_t& slot) noexcept {
    const auto first = state.targets.begin();
    const auto last = state.targets.end();
    const auto found = std::lower_bound(first, last, hash);
    if (found == last || *found != hash) {
        return false;
    }
    slot = static_cast<std::size_t>(found - first);
    return true;
}

} // namespace

void reset(Storage& storage) noexcept {
    storage.targets.fill(0);
    storage.targetCount = 0;
    storage.resolved.fill(names_state::Name{});
    storage.resolvedCount = 0;
    storage.tagCount = 0;
    storage.cursor = 0;
    storage.collected = false;
    storage.invalid = false;
}

bool collect_targets(Storage& storage) noexcept {
    storage.targets.fill(0);
    storage.targetCount = 0;
    static std::array<layouts::Definition, layouts::kDefinitionCapacity> scratch{};
    std::size_t rows = 0;
    if (!state::build_data::snapshot_scenario_layouts(scratch, rows)) {
        return false;
    }
    for (std::size_t row = 0; row < rows; ++row) {
        const layouts::Definition& layout = scratch[row];
        const std::size_t declared =
            (std::min)(static_cast<std::size_t>(layout.bubbleCount), layout.bubbleHashes.size());
        for (std::size_t bubble = 0; bubble < declared; ++bubble) {
            const std::uint32_t hash = layout.bubbleHashes[bubble];
            if (hash == 0 || storage.targetCount >= storage.targets.size()) {
                continue;
            }
            storage.targets[storage.targetCount++] = hash;
        }
    }
    static std::array<spawn_state::NameHash, spawn_state::kNameHashCapacity> spawns{};
    std::size_t spawnCount = 0;
    if (state::build_data::snapshot_spawn_name_hashes(spawns, spawnCount)) {
        for (std::size_t row = 0; row < spawnCount; ++row) {
            if (spawns[row].value != 0 && storage.targetCount < storage.targets.size()) {
                storage.targets[storage.targetCount++] = spawns[row].value;
            }
        }
    }
    auto end = storage.targets.begin() + static_cast<std::ptrdiff_t>(storage.targetCount);
    std::sort(storage.targets.begin(), end);
    storage.targetCount = static_cast<std::size_t>(std::unique(storage.targets.begin(), end)
                                                   - storage.targets.begin());
    return storage.targetCount != 0;
}

void offer(MatchState& state, std::string_view text) noexcept {
    if (text.empty() || text.size() > kRunCapacity) {
        return;
    }
    std::array<std::size_t, kTokenCapacity> starts{};
    std::size_t tokens = 0;
    starts[tokens++] = 0;
    for (std::size_t index = 0; index < text.size() && tokens < starts.size(); ++index) {
        if (text[index] == '_') {
            starts[tokens++] = index + 1;
        }
    }
    for (std::size_t first = 0; first < tokens; ++first) {
        for (std::size_t last = tokens; last > first; --last) {
            const std::size_t begin = starts[first];
            const std::size_t end = last == tokens ? text.size() : starts[last] - 1;
            if (end <= begin) {
                continue;
            }
            const std::string_view candidate = text.substr(begin, end - begin);
            std::size_t slot = 0;
            if (target_slot(state, hash_of(candidate), slot)) {
                keep(state, candidate, slot);
            }
        }
    }
}

void offer_blob(MatchState& state, std::span<const std::byte> blob) noexcept {
    std::size_t start = 0;
    std::size_t length = 0;
    for (std::size_t index = 0; index <= blob.size(); ++index) {
        const char character = index < blob.size() ? static_cast<char>(blob[index]) : '\0';
        if (index < blob.size() && names_state::name_character(character)) {
            if (length == 0) {
                start = index;
            }
            ++length;
            continue;
        }
        if (length != 0 && length <= kRunCapacity) {
            offer(state,
                  std::string_view(reinterpret_cast<const char*>(blob.data()) + start, length));
        }
        length = 0;
    }
}

MatchState match_state(Storage& storage) noexcept {
    return MatchState{{storage.targets.data(), storage.targetCount},
                      {storage.resolved.data(), storage.targetCount}};
}

void merge(Storage& storage, std::span<const names_state::Name> rows) noexcept {
    const std::size_t count = (std::min)(rows.size(), storage.targetCount);
    for (std::size_t slot = 0; slot < count; ++slot) {
        const names_state::Name& candidate = rows[slot];
        names_state::Name& row = storage.resolved[slot];
        if (candidate.nameLength == 0
            || (row.nameLength != 0 && row.nameLength <= candidate.nameLength)) {
            continue;
        }
        row = candidate;
    }
}

bool add_tag(Storage& storage, std::uint32_t tag) noexcept {
    if (storage.collected || storage.invalid || tag == 0) {
        return false;
    }
    if (storage.tagCount >= storage.tags.size()) {
        storage.invalid = true;
        return false;
    }
    storage.tags[storage.tagCount++] = tag;
    return true;
}

void finish(Storage& storage) noexcept {
    std::size_t kept = 0;
    for (std::size_t row = 0; row < storage.targetCount; ++row) {
        if (storage.resolved[row].nameLength != 0) {
            storage.resolved[kept++] = storage.resolved[row];
        }
    }
    for (std::size_t row = kept; row < storage.resolved.size(); ++row) {
        storage.resolved[row] = {};
    }
    storage.resolvedCount = kept;
}

} // namespace dawn::client::content::hash_names
