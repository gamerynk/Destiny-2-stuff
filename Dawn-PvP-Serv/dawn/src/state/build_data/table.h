#pragma once

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace dawn::state::build_data {

// the lock is apart from the storage so a domain with several arrays holds one lock across all,
// and a reader never sees one array replaced and another not
class Lock final {
public:
    class Shared final {
    public:
        explicit Shared(const Lock& owner) noexcept : owner_(owner) {
            AcquireSRWLockShared(&owner_.lock_);
        }
        ~Shared() {
            ReleaseSRWLockShared(&owner_.lock_);
        }
        Shared(const Shared&) = delete;
        Shared(Shared&&) = delete;
        Shared& operator=(const Shared&) = delete;
        Shared& operator=(Shared&&) = delete;

    private:
        const Lock& owner_;
    };

    class Exclusive final {
    public:
        explicit Exclusive(Lock& owner) noexcept : owner_(owner) {
            AcquireSRWLockExclusive(&owner_.lock_);
        }
        ~Exclusive() {
            ReleaseSRWLockExclusive(&owner_.lock_);
        }
        Exclusive(const Exclusive&) = delete;
        Exclusive(Exclusive&&) = delete;
        Exclusive& operator=(const Exclusive&) = delete;
        Exclusive& operator=(Exclusive&&) = delete;

    private:
        Lock& owner_;
    };

private:
    mutable SRWLOCK lock_{SRWLOCK_INIT};
};

// caller must hold the domain's Lock; storage is a member array, so nothing here allocates
template <typename Row, std::size_t Capacity> class Table final {
public:
    static constexpr std::size_t capacity = Capacity;

    void clear() noexcept {
        std::fill_n(rows_.begin(), count_, Row{});
        count_ = 0;
    }

    [[nodiscard]] bool replace(std::span<const Row> rows) noexcept {
        if (rows.size() > Capacity) {
            return false;
        }
        std::copy(rows.begin(), rows.end(), rows_.begin());
        if (count_ > rows.size()) {
            std::fill(rows_.begin() + static_cast<std::ptrdiff_t>(rows.size()),
                      rows_.begin() + static_cast<std::ptrdiff_t>(count_),
                      Row{});
        }
        count_ = rows.size();
        return true;
    }

    [[nodiscard]] std::span<Row> reset(std::size_t count) noexcept {
        if (count > Capacity) {
            return {};
        }
        std::fill_n(rows_.begin(), (std::max)(count_, count), Row{});
        count_ = count;
        return {rows_.data(), count_};
    }

    [[nodiscard]] bool snapshot(std::span<Row> output, std::size_t& count) const noexcept {
        count = 0;
        if (output.size() < count_) {
            return false;
        }
        std::copy_n(rows_.begin(), count_, output.begin());
        count = count_;
        return true;
    }

    [[nodiscard]] std::span<const Row> rows() const noexcept {
        return {rows_.data(), count_};
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return count_;
    }

private:
    std::array<Row, Capacity> rows_{};
    std::size_t count_{};
};

} // namespace dawn::state::build_data
