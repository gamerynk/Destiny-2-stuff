#pragma once

#include <Windows.h>

namespace dawn::core::threading {

class SrwLock final {
public:
    constexpr explicit SrwLock() noexcept = default;

    SrwLock(const SrwLock&) = delete;
    SrwLock(SrwLock&&) = delete;
    SrwLock& operator=(const SrwLock&) = delete;
    SrwLock& operator=(SrwLock&&) = delete;

    void lock() noexcept {
        AcquireSRWLockExclusive(&lock_);
    }

    [[nodiscard]] bool try_lock() noexcept {
        return TryAcquireSRWLockExclusive(&lock_);
    }

    void unlock() noexcept {
        ReleaseSRWLockExclusive(&lock_);
    }

    void lock_shared() noexcept {
        AcquireSRWLockShared(&lock_);
    }

    [[nodiscard]] bool try_lock_shared() noexcept {
        return TryAcquireSRWLockShared(&lock_);
    }

    void unlock_shared() noexcept {
        ReleaseSRWLockShared(&lock_);
    }

private:
    SRWLOCK lock_{SRWLOCK_INIT};
};

} // namespace dawn::core::threading
