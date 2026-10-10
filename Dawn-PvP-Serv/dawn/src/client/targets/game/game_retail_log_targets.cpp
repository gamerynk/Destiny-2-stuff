#include "../../patterns/game.h"
#include "resolution/internal.h"
#include "retail_log.h"

namespace dawn::client::targets::game::retail_log {
namespace {

Targets g_targets;
bool g_resolved{};

[[nodiscard]] constexpr std::size_t index(patterns::game::Id id) noexcept {
    return static_cast<std::size_t>(id) - resolution::kRetailLogFirstMatch;
}

} // namespace

bool derive(std::span<const patterns::Match> matches, Targets& output) noexcept {
    output = {};
    if (matches.size() != resolution::kRetailLogMatchCount) {
        return false;
    }
    for (const patterns::Match match : matches) {
        if (match.status != patterns::MatchStatus::unique) {
            return false;
        }
    }

    Targets resolved;
    resolved.enqueue = matches[index(patterns::game::Id::retailLogEnqueue)].address;
    resolved.setCategoryVerbosity =
        matches[index(patterns::game::Id::retailLogSetCategoryVerbosity)].address;
    output = resolved;
    return true;
}

void publish(const Targets& targets) noexcept {
    g_targets = targets;
    g_resolved = true;
}

void clear() noexcept {
    g_targets = {};
    g_resolved = false;
}

const Targets& get() noexcept {
    return g_targets;
}

bool is_resolved() noexcept {
    return g_resolved;
}

} // namespace dawn::client::targets::game::retail_log
