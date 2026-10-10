#pragma once
#include "../../state/activity/crucible/match_host.h"
#include "../../state/activity/crucible/completion_timing.h"
#include <cstring>

namespace dawn::client::crucible::projection {
namespace host = state::activity::crucible::host;
inline constexpr std::size_t kModelSize = 0x2260;
inline constexpr std::size_t kRowStride = 0x1C8;
inline constexpr std::uint64_t kFreshMs = 10'000;
inline constexpr std::array<std::uint32_t, 3> kStatKeys{0x8EF21B81, 0x232708FC, 0xCFC7F8D0};
struct Column {
    std::uint32_t table{}, key{}, labelTable{}, label{}, descriptionTable{}, description{};
    std::uint8_t format{}, padding[3]{};
};
static_assert(sizeof(Column) == 28);
using Columns = std::array<Column, 3>;
struct Headline { std::uint32_t table{}, key{}; };
using PlaceLabels = std::array<Headline, 16>;
// authored PC86657 first through eighth place labels, then their tied variants
inline constexpr std::array<std::uint32_t, 16> kPlaceKeys{
    0x6355CF95, 0x505E00D9, 0x6A5A5870, 0x5A628DC1,
    0x546045C8, 0x66671DD3, 0x6864E222, 0x5A4478FD,
    0xC1AA55CA, 0xA5D0ED26, 0xFA449BBD, 0x06B7653E,
    0x3F9EEDA5, 0x24F86F98, 0x7902227F, 0x5FF9E882};

inline const host::Row* member(const host::View& view, std::uint64_t key) noexcept {
    if (!key || view.count > view.rows.size()) return nullptr;
    for (std::size_t i = 0; i < view.count; ++i)
        if (view.rows[i].player == key) return &view.rows[i];
    return nullptr;
}
inline bool fresh(const host::View& view, std::uint64_t received, std::uint64_t now) noexcept {
    return view.key.valid() && view.count && view.count <= view.rows.size()
        && (view.phase == host::Phase::playing || view.phase == host::Phase::results)
        && received && now >= received && now - received <= kFreshMs;
}
struct HudScores { float local{}, opponent{}; };
inline bool hud_scores(const host::View& view, std::uint64_t localKey, HudScores& result) noexcept {
    const auto* self = member(view, localKey);
    if (!self || !view.scoreLimit || self->side >= view.scores.size()) return false;
    const bool rumble = view.mode == state::activity::crucible::match_rules::Mode::rumble;
    if (!rumble && view.mode != state::activity::crucible::match_rules::Mode::clash) return false;
    HudScores next{static_cast<float>(rumble ? self->score : view.scores[self->side]), 0};
    for (std::size_t i = 0; i < view.count; ++i) {
        const auto& other = view.rows[i];
        if (other.side >= view.scores.size()) return false;
        if (other.player == localKey || (!rumble && other.side == self->side)) continue;
        const auto score = static_cast<float>(rumble ? other.score : view.scores[other.side]);
        if (score > next.opponent) next.opponent = score;
    }
    result = next; return true;
}
inline bool place_label(const host::View& view, std::uint64_t localKey, std::size_t& label) noexcept {
    const auto* self = member(view, localKey);
    if (!self || view.phase != host::Phase::playing
        || view.mode != state::activity::crucible::match_rules::Mode::rumble) return false;
    std::size_t ahead = 0, equal = 0;
    for (std::size_t i = 0; i < view.count; ++i) {
        const auto& row = view.rows[i];
        if (!row.player) return false;
        for (std::size_t j = 0; j < i; ++j) if (view.rows[j].player == row.player) return false;
        ahead += row.score > self->score;
        equal += row.score == self->score;
    }
    if (ahead >= 8) return false;
    label = ahead + (equal > 1 ? 8 : 0);
    return true;
}
// polls refresh remaining play time; they never restart a native end-phase countdown
struct Countdown {
    host::Key key{};
    std::uint8_t phase{};
    std::uint64_t started{};
    bool sample(const host::View& view, std::uint64_t received, std::uint64_t now,
                std::uint8_t nativePhase, std::uint64_t& remainingMs) noexcept {
        if (!fresh(view, received, now)) return false;
        if (!(key == view.key) || phase != nativePhase) {
            key = view.key; phase = nativePhase; started = now;
        }
        if (nativePhase == 3 && view.phase == host::Phase::playing) {
            const auto age = now - received;
            remainingMs = view.remainingMs > age ? view.remainingMs - age : 0;
            return true;
        }
        if (nativePhase == 6 || nativePhase == 7) {
            const auto duration = state::activity::crucible::completion::duration(nativePhase);
            const auto age = now >= started ? now - started : 0;
            remainingMs = duration > age ? duration - age : 0;
            return true;
        }
        return false;
    }
};
template<class T> inline T read(std::span<const std::byte> model, std::size_t offset) noexcept {
    T value{}; std::memcpy(&value, model.data() + offset, sizeof value); return value;
}
template<class T> inline void write(std::span<std::byte> model, std::size_t offset, const T& value) noexcept {
    std::memcpy(model.data() + offset, &value, sizeof value);
}
inline bool apply_place(std::span<std::byte> model, const host::View& view, std::uint64_t localKey,
                        const PlaceLabels& labels) noexcept {
    std::size_t index{};
    if (model.size() < kModelSize || read<std::uint8_t>(model, 0x2114) != 3
        || !place_label(view, localKey, index)) return false;
    const auto& label = labels[index];
    if (!label.table || label.table == 0xFFFFFFFF || label.key != kPlaceKeys[index]) return false;
    write(model, 0x2080, label);
    write(model, 0x2088, std::uint32_t{0});
    // the formatter caches by this token, so a changed place or tie must invalidate its text
    write(model, 0x2078, std::uint64_t{0x4441574E00000000ULL} | label.key);
    return true;
}
// only scores are projected; identity, names, emblems and row order stay native. Authenticate
// the whole row mapping before changing any display field
inline bool apply(std::span<std::byte> model, const host::View& view, std::uint64_t localKey,
                  const Columns& columns) noexcept {
    if (model.size() < kModelSize || !member(view, localKey)) return false;
    const auto count = read<std::uint32_t>(model, 0);
    if (!count || count > 16 || count != view.count) return false;
    for (std::size_t c = 0; c < columns.size(); ++c)
        if (!columns[c].table || columns[c].key != kStatKeys[c]) return false;
    std::array<const host::Row*, 16> rows{};
    std::uint32_t localIndex = 16;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto key = read<std::uint64_t>(model, i * kRowStride + 0x50);
        rows[i] = member(view, key);
        if (!rows[i] || rows[i]->side >= view.scores.size()) return false;
        for (std::uint32_t j = 0; j < i; ++j) if (rows[i] == rows[j]) return false;
        if (key == localKey) localIndex = i;
    }
    if (localIndex == 16) return false;
    write(model, 0x1DD4, std::uint32_t{3});
    write(model, 0x1DD8, columns);
    write(model, 0x1DB8, static_cast<float>(view.scoreLimit));
    write(model, 0x2044, std::uint8_t(view.mode == state::activity::crucible::match_rules::Mode::rumble));
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& row = *rows[i]; const auto offset = i * kRowStride;
        write(model, offset + 0xEC, std::uint32_t{3});
        write(model, offset + 0xF0, std::array<float, 3>{
            static_cast<float>(row.score), static_cast<float>(row.kills), static_cast<float>(row.deaths)});
        write(model, offset + 0x14C, static_cast<float>(row.score));
    }
    write(model, 0x2070, localIndex);
    write(model, 0x2068, static_cast<float>(rows[localIndex]->score));
    write(model, 0x2064, static_cast<float>(view.scores[rows[localIndex]->side]));
    return true;
}
}
