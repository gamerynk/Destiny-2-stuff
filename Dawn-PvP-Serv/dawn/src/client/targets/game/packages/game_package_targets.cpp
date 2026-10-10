#include "game_package_targets.h"

#include <array>

#include "../../../memory/current_process_memory.h"
#include "../../../patterns/game/packages/package_key_signature_bytes.h"
#include "../relative.h"

namespace dawn::client::targets::game::packages {
namespace {

constexpr std::size_t kTableDisplacementOffset = 3;
constexpr std::size_t kTableInstructionEndOffset = 7;

Targets g_targets;
bool g_resolved{};

} // namespace

bool derive(std::span<const patterns::ImageRange> image, Targets& output) noexcept {
    output = {};
    const patterns::Pattern pattern{"package_key_table", patterns::game::packages::kKeyTable};
    std::array<patterns::Match, 1> match{};
    if (!patterns::resolve_all(image, std::span(&pattern, 1), match)
        || match[0].status != patterns::MatchStatus::unique) {
        return false;
    }
    Targets resolved;
    if (!relative::resolve(match[0].address,
                           kTableDisplacementOffset,
                           kTableInstructionEndOffset,
                           resolved.keyTable)) {
        return false;
    }
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

bool read(KeyTable& table) noexcept {
    table = {};
    if (!g_resolved || g_targets.keyTable == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(g_targets.keyTable);
    return memory::read_current_process(
        nullptr, base, std::as_writable_bytes(std::span{&table, 1}));
}

} // namespace dawn::client::targets::game::packages
