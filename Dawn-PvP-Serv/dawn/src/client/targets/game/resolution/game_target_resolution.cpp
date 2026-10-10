#include "game_target_resolution.h"

#include <array>

#include "internal.h"

namespace dawn::client::targets::game::resolution {
namespace {

Failure g_lastFailure{Failure::none};
std::string_view g_lastFailedSignature{};

// clear the content commit marker before invalidating any target table
void clear_groups() noexcept {
    config_getter::clear();
    packages::clear();
    assert_handler::clear();
    retail_log::clear();
    content::clear();
    network::clear();
}

[[nodiscard]] bool fail_resolution(Failure failure) noexcept {
    g_lastFailure = failure;
    clear_groups();
    return false;
}

[[nodiscard]] std::string_view first_unresolved(std::span<const patterns::Pattern> definitions,
                                                std::span<const patterns::Match> matches) noexcept {
    for (std::size_t index = 0; index < matches.size(); ++index) {
        if (matches[index].status != patterns::MatchStatus::unique) {
            return index < definitions.size() ? definitions[index].name : std::string_view{};
        }
    }
    return {};
}

} // namespace

bool resolve(std::span<const patterns::ImageRange> image) noexcept {
    clear_groups();
    g_lastFailure = Failure::none;
    g_lastFailedSignature = {};
    const std::span<const patterns::Pattern> definitions = patterns::game::definitions();
    std::array<patterns::Match, kMatchCount> matches{};
    if (definitions.size() != matches.size()
        || !patterns::resolve_all(image, definitions, matches)) {
        return fail_resolution(Failure::signature);
    }
    const std::span<const patterns::Pattern> required = definitions.first(kRequiredMatchCount);
    g_lastFailedSignature = first_unresolved(
        required, std::span<const patterns::Match>(matches).first(kRequiredMatchCount));
    if (!g_lastFailedSignature.empty()) {
        return fail_resolution(Failure::signature);
    }

    network::Targets networkTargets;
    content::Targets contentTargets;
    const std::span<const patterns::Match> resolvedMatches = matches;
    // a rejection here is a derived address or page check, not a signature miss
    if (!network::derive(image, resolvedMatches.first(kNetworkMatchCount), networkTargets)) {
        return fail_resolution(Failure::networkDerive);
    }
    if (!content::derive(image,
                         resolvedMatches.subspan(kContentFirstMatch, kContentMatchCount),
                         contentTargets)) {
        return fail_resolution(Failure::contentDerive);
    }

    network::publish(networkTargets);
    // content readiness is the commit marker once the network table is visible
    content::publish(contentTargets);
    // diagnostic only: a miss leaves capture off and never fails activation
    retail_log::Targets retailLogTargets;
    if (retail_log::derive(resolvedMatches.subspan(kRetailLogFirstMatch), retailLogTargets)) {
        retail_log::publish(retailLogTargets);
    }
    // also diagnostic, but without it an assert halts the boot behind the game's own dialog
    assert_handler::Targets assertTargets;
    if (assert_handler::derive(image, assertTargets)) {
        assert_handler::publish(assertTargets);
    }
    packages::Targets packageTargets;
    if (packages::derive(image, packageTargets)) {
        packages::publish(packageTargets);
    }
    config_getter::Targets configGetterTargets;
    if (config_getter::derive(image, configGetterTargets)) {
        config_getter::publish(configGetterTargets);
    }
    return true;
}

Failure last_failure() noexcept {
    return g_lastFailure;
}

std::string_view last_failed_signature() noexcept {
    return g_lastFailedSignature;
}

} // namespace dawn::client::targets::game::resolution
