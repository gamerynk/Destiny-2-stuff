#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace dawn::state::activity::coo {
inline constexpr std::uint8_t kNoDialogue = 0xFFU;
struct DialogueRow final {
    std::uint32_t selector, durationMs, delayMs;
    bool sceneOwned;
};
struct ObjectiveCueBinding final { std::uint8_t row; std::uint32_t objective; };
struct DialogueDefinition final {
    std::uint32_t bank;
    std::span<const DialogueRow> rows;
    std::span<const ObjectiveCueBinding> objectiveCues;
    std::uint32_t dispatchTimeoutMs{15000}, spacingMs{250};
};

template<std::size_t Rows>
class DialogueService final {
    static_assert(Rows > 0 && Rows <= 64);
public:
    [[nodiscard]] bool due(std::uint64_t now) const noexcept {
        for (const auto& cue : queue_) { if (cue.used && now >= cue.due) { return true; } }
        return false;
    }
    [[nodiscard]] std::uint64_t voice_until() const noexcept { return voiceUntil_; }
    [[nodiscard]] std::uint32_t timed_out() const noexcept { return timedOut_; }
    void discard_before(std::uint8_t stage) noexcept {
        for (auto& cue : queue_) { if (cue.used && cue.stage < stage) { cue.used = false; } }
    }
    template<class Presentation>
    void silence(Presentation& presentation, std::uint32_t& revision) noexcept {
        queue_ = {}; presentation.activeRow = kNoDialogue; ++revision;
    }
    void enqueue(const DialogueDefinition& policy, std::uint8_t row, std::uint64_t now,
                 std::uint64_t delay, std::uint8_t stage, std::uint32_t& revision) noexcept {
        if (row >= Rows || row >= policy.rows.size() || policy.rows[row].durationMs == 0
            || policy.rows[row].sceneOwned || (requested_ & (1ULL << row)) != 0) { return; }
        for (auto& cue : queue_) {
            if (!cue.used) {
                cue = {now + delay, ++order_, row, stage, true};
                requested_ |= 1ULL << row; ++revision; return;
            }
        }
    }
    template<class Presentation>
    void objective(const DialogueDefinition& policy, std::uint32_t event,
                   Presentation& presentation, std::uint32_t& revision) noexcept {
        if (presentation.objective == event) { return; }
        presentation.objective = event;
        for (auto& cue : queue_) {
            for (const auto& binding : policy.objectiveCues) {
                if (cue.used && cue.row == binding.row && event != binding.objective) { cue.used = false; }
            }
        }
        ++revision;
    }
    template<class Presentation>
    void advance(const DialogueDefinition& policy, std::uint64_t run, std::uint64_t now,
                 bool blocked, Presentation& presentation, std::uint32_t& revision,
                 bool retainUnsubmitted=false) noexcept {
        if (presentation.activeRow != kNoDialogue) {
            if (now - offeredAt_ < policy.dispatchTimeoutMs) { return; }
            // required speech stays offered across a temporary native ownership loss;
            // keep its generation so recovery cannot replay it
            if (retainUnsubmitted) { offeredAt_=now;++timedOut_;++revision;return; }
            presentation.activeRow = kNoDialogue; ++timedOut_; ++revision;
        }
        if (now < voiceUntil_ || blocked) { return; }
        Cue* next{};
        for (auto& cue : queue_) {
            if (cue.used && now >= cue.due && (!next || cue.order < next->order)) { next = &cue; }
        }
        if (!next) { return; }
        presentation.activeRow = next->row;
        presentation.generations[next->row] = static_cast<std::uint32_t>(run % 0x7FFFFFFEULL) + 1;
        offeredAt_ = now; next->used = false; ++revision;
    }
    template<class Presentation>
    [[nodiscard]] bool submitted(const DialogueDefinition& policy, std::uint32_t bank,
                                 std::uint8_t row, std::uint32_t generation, std::uint64_t now,
                                 Presentation& presentation, std::uint32_t& revision) noexcept {
        if (bank != policy.bank || row >= Rows || row >= policy.rows.size()
            || presentation.activeRow != row || presentation.generations[row] != generation) { return false; }
        voiceUntil_ = now + policy.rows[row].durationMs + policy.rows[row].delayMs + policy.spacingMs;
        presentation.activeRow = kNoDialogue; ++revision; return true;
    }
private:
    struct Cue final {
        std::uint64_t due{};
        std::uint32_t order{};
        std::uint8_t row{}, stage{};
        bool used{};
    };
    std::array<Cue, Rows> queue_{};
    std::uint64_t requested_{}, offeredAt_{}, voiceUntil_{};
    std::uint32_t order_{}, timedOut_{};
};
} // namespace dawn::state::activity::coo
