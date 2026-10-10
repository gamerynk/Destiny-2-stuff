#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::activity::coo {
template<class Receipt>
struct ReadinessRequest final {
    std::uint64_t run{};
    std::array<Receipt, 12> actors{};
    std::size_t count{};
};

class ReadinessSchedule final {
public:
    void reset() noexcept { *this = {}; }
    template<class Receipt, std::size_t Capacity, class Enumerate>
    [[nodiscard]] ReadinessRequest<Receipt> request(std::uint64_t run,
        std::uint64_t now, Enumerate enumerate, std::size_t budget = 12) noexcept {
        static_assert(Capacity > 0);
        ReadinessRequest<Receipt> result{};
        if (!run || !budget || budget > result.actors.size()) return result;
        if (run_ != run) { reset(); run_ = run; }
        if (sampled_ && now < next_) return result;
        sampled_ = true;
        next_ = now > UINT64_MAX - 500 ? UINT64_MAX : now + 500;
        std::array<Receipt, Capacity> pending{};
        std::size_t count{};
        enumerate([&](const Receipt& receipt) noexcept {
            if (count < pending.size()) pending[count++] = receipt;
        });
        result.run = run;
        if (!count) { cursor_ = 0; return result; }
        const auto begin = cursor_ % count;
        result.count = count < budget ? count : budget;
        for (std::size_t i = 0; i < result.count; ++i)
            result.actors[i] = pending[(begin + i) % count];
        cursor_ = (begin + result.count) % count;
        return result;
    }
private:
    std::uint64_t run_{}, next_{};
    std::size_t cursor_{};
    bool sampled_{};
};
} // namespace dawn::state::activity::coo
