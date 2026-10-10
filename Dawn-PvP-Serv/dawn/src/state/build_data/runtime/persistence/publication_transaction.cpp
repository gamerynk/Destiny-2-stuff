#include "publication_transaction.h"

#include <Windows.h>

#include "build_data_persistence.h"

namespace dawn::state::build_data::runtime::persistence {

Transaction::Transaction() noexcept : state_(&context()) {
    AcquireSRWLockExclusive(&state_->lock);
    if (state_->persisted) {
        // a saved cache is one fixed build snapshot, so later extraction must not change State
        ReleaseSRWLockExclusive(&state_->lock);
        state_ = nullptr;
        return;
    }
    active_ = true;
}

Transaction::~Transaction() {
    if (active_) {
        ReleaseSRWLockExclusive(&state_->lock);
    }
}

bool Transaction::active() const noexcept {
    return active_;
}

bool Transaction::finish(bool published, Rollback& rollback) noexcept {
    if (!active_) {
        return false;
    }
    bool result = published;
    if (result && !persist_if_complete_locked(*state_)) {
        // a failed write must not leave the candidate visible as a published domain
        rollback();
        result = false;
    }
    ReleaseSRWLockExclusive(&state_->lock);
    active_ = false;
    state_ = nullptr;
    return result;
}

} // namespace dawn::state::build_data::runtime::persistence
