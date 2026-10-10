#include "queuez_state_validation.h"

#include <cstddef>

namespace dawn::server::bap::encrypted::queuez {
namespace {

[[nodiscard]] bool valid_phase(Family3Phase phase) noexcept {
    return phase == Family3Phase::normal || phase == Family3Phase::publishOnce
           || phase == Family3Phase::responseOnly;
}

} // namespace

bool valid(const SessionState& state) noexcept {
    if (!valid_phase(state.family3Phase)) {
        return false;
    }
    // Family 3 has its own store and ladder and can go active one frame before the Family-4
    // companion during boot, but an active pair must share one root
    if (state.family3Active) {
        if (state.family3RootSoid == 0 || state.family3Version < kInitialFamilyVersion) {
            return false;
        }
    } else if (state.family3RootSoid != 0 || state.family3Version != kInitialFamilyVersion
               || state.family3Phase != Family3Phase::normal) {
        return false;
    }
    if (state.family0Active) {
        if (state.family0Character == 0 || state.family0Version < kInitialFamilyVersion) {
            return false;
        }
    } else if (state.family0Character != 0 || state.family0Version != kInitialFamilyVersion) {
        return false;
    }
    if (!state.family4Active) {
        if (state.family4RootSoid != 0 || state.family4Version != kInitialFamilyVersion
            || state.family4ResidentCount != 0 || state.family3Phase != Family3Phase::normal) {
            return false;
        }
        for (const ResidentObject& object : state.family4Residents) {
            if (object.objectSoid != 0 || object.definitionId != 0) {
                return false;
            }
        }
        return true;
    }
    if (state.family4RootSoid == 0 || state.family4ResidentCount == 0
        || state.family4ResidentCount > state.family4Residents.size()) {
        return false;
    }
    if (state.family3Active && state.family3RootSoid != state.family4RootSoid) {
        return false;
    }
    if (state.family4Version < kInitialFamilyVersion
        || (state.family4Version == kInitialFamilyVersion
            && state.family3Phase != Family3Phase::normal)) {
        return false;
    }
    for (std::size_t index = 0; index < state.family4ResidentCount; ++index) {
        const ResidentObject& object = state.family4Residents[index];
        if (object.objectSoid == 0 || object.definitionId == 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.family4Residents[prior].objectSoid == object.objectSoid) {
                return false;
            }
        }
    }
    for (std::size_t index = state.family4ResidentCount; index < state.family4Residents.size();
         ++index) {
        const ResidentObject& object = state.family4Residents[index];
        if (object.objectSoid != 0 || object.definitionId != 0) {
            return false;
        }
    }
    return state.family4Residents.front().objectSoid == state.family4RootSoid;
}

} // namespace dawn::server::bap::encrypted::queuez
