#include "queuez_family_staging.h"

#include <cstddef>
#include <limits>

#include "../queuez_state_validation.h"

namespace dawn::server::bap::encrypted::queuez {

bool staging::same_resident(const ResidentObject& left, const ResidentObject& right) noexcept {
    return left.objectSoid == right.objectSoid && left.definitionId == right.definitionId;
}

bool staging::same_state(const SessionState& left, const SessionState& right) noexcept {
    if (!valid(left) || !valid(right) || left.family4RootSoid != right.family4RootSoid
        || left.family3RootSoid != right.family3RootSoid
        || left.family4Version != right.family4Version
        || left.family3Version != right.family3Version
        || left.family0Version != right.family0Version
        || left.family0Character != right.family0Character
        || left.family4ResidentCount != right.family4ResidentCount
        || left.family3Phase != right.family3Phase || left.family4Active != right.family4Active
        || left.family3Active != right.family3Active || left.family0Active != right.family0Active) {
        return false;
    }
    for (std::size_t index = 0; index < left.family4Residents.size(); ++index) {
        if (!staging::same_resident(left.family4Residents[index], right.family4Residents[index])) {
            return false;
        }
    }
    return true;
}

namespace {

[[nodiscard]] bool same_manifest(const SessionState& state,
                                 const SessionState& candidate) noexcept {
    if (!valid(state) || !valid(candidate) || state.family4RootSoid != candidate.family4RootSoid
        || state.family4Version != candidate.family4Version
        || state.family4ResidentCount != candidate.family4ResidentCount) {
        return false;
    }
    for (std::size_t index = 0; index < state.family4ResidentCount; ++index) {
        if (!staging::same_resident(state.family4Residents[index],
                                    candidate.family4Residents[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool stage_family4_snapshot(const SessionState& before,
                            const middleware::queuez::Family& family,
                            SessionState& after) noexcept {
    after = before;
    if (!valid(before) || family.type != kAccountFamilyType || family.rootSoid == 0
        || family.version != kInitialFamilyVersion
        || family.flags != middleware::queuez::kFullSnapshotFlag || family.objects.empty()
        || family.objects.size() > kResidentCapacity
        || family.objects.size()
               > static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())) {
        return false;
    }

    SessionState candidate = before;
    candidate.family4Residents = {};
    candidate.family4RootSoid = family.rootSoid;
    candidate.family4Version = family.version;
    candidate.family4ResidentCount = static_cast<std::uint16_t>(family.objects.size());
    candidate.family4Active = true;
    for (std::size_t index = 0; index < family.objects.size(); ++index) {
        const middleware::queuez::Object& object = family.objects[index];
        if (object.id == 0 || object.version == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (family.objects[prior].version == object.version) {
                return false;
            }
        }
        candidate.family4Residents[index] = ResidentObject{object.version, object.id};
    }
    if (candidate.family4Residents.front().objectSoid != family.rootSoid) {
        return false;
    }
    if (!valid(candidate)) {
        return false;
    }
    if (before.family4Active) {
        return before.family4Version == kInitialFamilyVersion && same_manifest(before, candidate);
    }
    if (before.family3Phase != Family3Phase::normal) {
        return false;
    }
    after = candidate;
    return true;
}

bool stage_family0_subscription(const SessionState& before,
                                std::uint64_t selectedCharacter,
                                bool& publish,
                                bool& incremental,
                                SessionState& after) noexcept {
    publish = false;
    incremental = false;
    after = before;
    if (!valid(before) || selectedCharacter == 0) {
        return false;
    }
    if (!before.family0Active) {
        publish = true;
        after.family0Active = true;
        after.family0Character = selectedCharacter;
        after.family0Version = kInitialFamilyVersion;
        return true;
    }
    if (before.family0Character == selectedCharacter) {
        return true;
    }
    publish = true;
    incremental = true;
    after.family0Character = selectedCharacter;
    after.family0Version = before.family0Version + 1;
    return true;
}

bool stage_family3_subscription(const SessionState& before,
                                const middleware::queuez::Subscription& subscription,
                                bool& publish,
                                SessionState& after) noexcept {
    publish = false;
    after = before;
    if (!valid(before) || subscription.familyType != kRosterFamilyType
        || subscription.familyRootSoid == 0) {
        return false;
    }
    if ((before.family4Active && subscription.familyRootSoid != before.family4RootSoid)
        || (before.family3Active && subscription.familyRootSoid != before.family3RootSoid)) {
        return false;
    }
    if (!before.family3Active) {
        // the caller installs this seed only after the full frame is copied; until then the before-image stays inactive
        publish = true;
        after.family3RootSoid = subscription.familyRootSoid;
        after.family3Version = kInitialFamilyVersion;
        after.family3Active = true;
        return valid(after);
    }
    if (before.family3Phase == Family3Phase::normal) {
        publish = true;
        after.family3Version = kInitialFamilyVersion;
        return valid(after);
    }
    if (!before.family4Active) {
        return false;
    }
    if (before.family3Phase == Family3Phase::publishOnce) {
        publish = true;
        after.family3Version = kInitialFamilyVersion;
        after.family3Phase = Family3Phase::responseOnly;
        return valid(after);
    }
    return before.family3Phase == Family3Phase::responseOnly;
}

void stage_unsubscription(const SessionState& before,
                          std::uint8_t familyType,
                          std::uint64_t familyRootSoid,
                          SessionState& after) noexcept {
    after = before;
    if (familyType == kAccountFamilyType && before.family4Active
        && familyRootSoid == before.family4RootSoid) {
        after.family4RootSoid = 0;
        after.family4Residents = {};
        after.family4Version = kInitialFamilyVersion;
        after.family4ResidentCount = 0;
        after.family4Active = false;
        after.family3Phase = Family3Phase::normal;
    } else if (familyType == kRosterFamilyType && before.family3Active
               && familyRootSoid == before.family3RootSoid) {
        after.family3RootSoid = 0;
        after.family3Version = kInitialFamilyVersion;
        after.family3Phase = Family3Phase::normal;
        after.family3Active = false;
    } else if (familyType == kBannerFamilyType
               && familyRootSoid == before.pendingBannerRoot) {
        after.pendingBannerRoot = 0;
    }
}

} // namespace dawn::server::bap::encrypted::queuez
