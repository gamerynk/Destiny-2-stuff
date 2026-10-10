#include "activity_defaults_validation.h"

#include <limits>
#include <string_view>

#include "../destination/activity_destination_validation.h"

namespace dawn::state::activity::defaults {
namespace {

[[nodiscard]] std::uint64_t bubble_mask(std::uint8_t bubbleCount) noexcept {
    if (bubbleCount == kBubbleCapacity) {
        return (std::numeric_limits<std::uint64_t>::max)();
    }
    return (std::uint64_t{1} << bubbleCount) - 1;
}

constexpr std::string_view kBundledPackageName = "city_tower_social_d2";
constexpr std::int8_t kBundledReason = -1;
constexpr std::int16_t kBundledPreviousActivityIndex = -1;
constexpr std::int16_t kBundledActivityIndex = 20;
constexpr std::uint8_t kBundledBubbleCount = 8;
constexpr std::uint64_t kBundledStatefulBubbleMask = 0xCF;
constexpr std::uint16_t kBundledInitialSliceSet = 48;
constexpr std::uint32_t kBundledSpawnSetHash = 0x811C9DC5;

static_assert(kBundledPackageName.size() <= destination::kPackageNameCapacity);

} // namespace

bool valid(const DefaultDestination& candidate) noexcept {
    const FallbackPolicy& fallback = candidate.fallback;
    if (!destination::valid(candidate.selection) || fallback.bubbleCount < kMinimumBubbleCount
        || fallback.bubbleCount > kBubbleCapacity
        || fallback.initialSliceSet > kMaximumInitialSliceSet
        || (fallback.statefulBubbleMask & ~bubble_mask(fallback.bubbleCount)) != 0) {
        return false;
    }

    const std::size_t initialBubble = fallback.initialSliceSet / kSliceStatesPerBubble;
    return initialBubble < fallback.bubbleCount
           && (fallback.statefulBubbleMask & (std::uint64_t{1} << initialBubble)) != 0;
}

bool valid(const ActivityDefaults& candidate) noexcept {
    return valid(candidate.defaultDestination);
}

ActivityDefaults authored() noexcept {
    ActivityDefaults result{};
    destination::DestinationSelection& selection = result.defaultDestination.selection;
    for (std::size_t index = 0; index < kBundledPackageName.size(); ++index) {
        selection.packageName[index] = static_cast<std::int8_t>(kBundledPackageName[index]);
    }
    selection.packageNameLength = static_cast<std::uint8_t>(kBundledPackageName.size());
    selection.reason = kBundledReason;
    selection.previousActivityIndex = kBundledPreviousActivityIndex;
    selection.activityIndex = kBundledActivityIndex;

    FallbackPolicy& fallback = result.defaultDestination.fallback;
    fallback.bubbleCount = kBundledBubbleCount;
    fallback.statefulBubbleMask = kBundledStatefulBubbleMask;
    fallback.initialSliceSet = kBundledInitialSliceSet;
    fallback.spawnSetHash = kBundledSpawnSetHash;
    return result;
}

} // namespace dawn::state::activity::defaults
