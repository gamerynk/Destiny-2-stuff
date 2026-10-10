#include "activity_destination_validation.h"

namespace dawn::state::activity::destination {

bool valid(const DestinationSelection& selection) noexcept {
    return selection.packageNameLength <= selection.packageName.size();
}

} // namespace dawn::state::activity::destination
