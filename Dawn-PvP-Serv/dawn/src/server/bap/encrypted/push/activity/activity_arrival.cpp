#include "activity_arrival.h"

#include "../../../../../middleware/content/packages/tables/region_reader.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace tables = middleware::content::packages::tables;
namespace scenarios = state::build_data::scenarios;

constexpr std::size_t kNoBubble = static_cast<std::size_t>(-1);

[[nodiscard]] std::string_view
authored_name(const state::activity::defaults::DefaultDestination& defaults) noexcept {
    return {reinterpret_cast<const char*>(defaults.selection.packageName.data()),
            defaults.selection.packageNameLength};
}

[[nodiscard]] std::size_t declared_bubbles(const scenarios::Definition& layout) noexcept {
    return (std::min)(static_cast<std::size_t>(layout.bubbleCount), layout.bubbleStates.size());
}

// the enabled state byte means the bubble has a slice set, the disabled byte means none
[[nodiscard]] bool bubble_is_live(const scenarios::Definition& layout,
                                  std::size_t bubble) noexcept {
    return bubble < declared_bubbles(layout)
           && layout.bubbleStates[bubble] == scenarios::kBubbleEnabledByte;
}

[[nodiscard]] std::size_t bubble_for_hash(const scenarios::Definition& layout,
                                          std::uint32_t hash) noexcept {
    if (hash == 0) {
        return kNoBubble;
    }
    const std::size_t count = declared_bubbles(layout);
    for (std::size_t bubble = 0; bubble < count; ++bubble) {
        if (layout.bubbleHashes[bubble] == hash) {
            return bubble;
        }
    }
    return kNoBubble;
}

[[nodiscard]] std::size_t first_live_bubble(const scenarios::Definition& layout) noexcept {
    const std::size_t count = declared_bubbles(layout);
    for (std::size_t bubble = 0; bubble < count; ++bubble) {
        if (layout.bubbleStates[bubble] == scenarios::kBubbleEnabledByte) {
            return bubble;
        }
    }
    return kNoBubble;
}

[[nodiscard]] std::size_t
resolve_arrival_bubble(const state::activity::defaults::DefaultDestination& defaults,
                       const state::activity::destination::DestinationSelection& selection,
                       std::string_view name,
                       const scenarios::Definition& layout) noexcept {
    if (selection.hasArrivalBubbleOverride) {
        return selection.arrivalBubbleOverride;
    }
    const std::size_t named = selection.hasArrivalBubbleHash
                                  ? bubble_for_hash(layout, selection.arrivalBubbleHash)
                                  : kNoBubble;
    if (named != kNoBubble) {
        return named;
    }
    // the authored index was measured on a live load of the default destination, so it wins there and only there
    // elsewhere it is another place's bubble under this name: a valid slice set that fails silently
    if (name == authored_name(defaults)) {
        return defaults.fallback.initialSliceSet / tables::kSliceSetIndexFactor;
    }
    return first_live_bubble(layout);
}

} // namespace

std::uint16_t arrival_slice_set(const state::activity::defaults::DefaultDestination& defaults,
                                const state::activity::destination::DestinationSelection& selection,
                                std::string_view name,
                                const scenarios::Definition& layout) noexcept {
    if (selection.hasSliceSetOverride) {
        return selection.sliceSetOverride;
    }
    // Tree of Probabilities opens at the Lighthouse, region 120; the layout's first live bubble is the boss ledge where opening populations never load
    if (name == "strike_pact") {
        return 120;
    }
    std::size_t bubble = resolve_arrival_bubble(defaults, selection, name, layout);
    // never fall back across destinations: another place's index is valid here and loads the wrong geometry with no error
    if (!bubble_is_live(layout, bubble)) {
        bubble = first_live_bubble(layout);
    }
    if (bubble == kNoBubble) {
        return defaults.fallback.initialSliceSet;
    }
    return static_cast<std::uint16_t>(tables::region_index(static_cast<std::uint32_t>(bubble)));
}

} // namespace dawn::server::bap::encrypted::push::activity
