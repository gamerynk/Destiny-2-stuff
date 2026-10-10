#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::state::activity::coo {

enum class Schema : std::uint8_t { unspecified, omegaArchive, otherMissions };
enum class Operation : std::uint8_t {
    scene, population, objective, dialogue, device, cinematic, traversal, mechanic, observation, eventAfter, complete
};
constexpr bool is_observation(Operation op) noexcept { return op==Operation::observation || op==Operation::eventAfter; }
enum class Milestone : std::uint8_t { nativeReady, completed, observed, failed };
enum class Wait : std::uint8_t { requested, nativeReady, completed, observed };
enum class Phase : std::uint8_t { idle, running, complete, cancelled, failed };
enum class Failure : std::uint8_t { none, definition, publication, queueOverflow, native };
enum class StepPhase : std::uint8_t { pending, active, complete, cancelled };

struct Asset final {
    std::uint32_t registry{}, definition{};
    std::uint16_t type{}, slot{};
    friend constexpr bool operator==(const Asset&, const Asset&) = default;
};
[[nodiscard]] inline const Asset* resolve(std::span<const Asset> catalog, Asset key) noexcept {
    const Asset* result{};
    for (const auto& asset : catalog) {
        if (asset == key) {
            if (result) { return nullptr; }
            result = &asset;
        }
    }
    return result;
}

struct CommandSpec final {
    Operation operation{};
    Asset asset{};
    std::uint32_t argument{};
    Wait wait{Wait::completed};
};
struct Step final {
    std::string_view name;
    std::uint32_t dependencies{};
    std::span<const CommandSpec> commands;
};
struct ReceiptBinding final { std::string_view name; std::uint8_t step{}, command{}; };
struct Definition final {
    std::string_view name;
    Schema schema{};
    std::span<const Step> steps;
    std::span<const ReceiptBinding> receipts{};
};
struct Token final {
    std::uint64_t run{}, incarnation{};
    std::uint8_t step{}, command{};
    friend constexpr bool operator==(const Token&, const Token&) = default;
};
struct Command final { Token token; Schema schema; CommandSpec spec; };
struct Event final { Token token; Milestone milestone; };

// called only from update()/cancel() on the owner thread; must not call back into the executor.
// publish means requested, never native-ready
struct Services {
    virtual bool publish(const Command&) noexcept = 0;
    virtual void cancel(const Command&) noexcept = 0;
    virtual ~Services() = default;
};

struct CommandState final { bool requested{}, ready{}, completed{}, retired{}; };
struct StepState final {
    StepPhase phase{};
    std::array<CommandState, 8> commands{};
};
struct Diagnostics final {
    Phase phase{};
    Failure failure{};
    std::uint64_t run{}, incarnation{}, rejected{}, duplicates{};
    std::uint32_t active{}, complete{};
    std::size_t queued{};
};

class Executor final {
public:
    static constexpr std::size_t kMaxSteps = 32, kMaxCommands = 8, kQueueSize = 128;

    [[nodiscard]] static bool valid(const Definition& definition) noexcept {
        if (definition.name.empty() || definition.schema == Schema::unspecified
            || definition.steps.empty() || definition.steps.size() > kMaxSteps) { return false; }
        for (std::size_t i = 0; i < definition.steps.size(); ++i) {
            const auto& step = definition.steps[i];
            const auto earlier = i == 0 ? 0U : (std::uint32_t{1} << i) - 1U;
            if (step.name.empty() || step.commands.empty() || step.commands.size() > kMaxCommands
                || (step.dependencies & ~earlier) != 0) { return false; }
            for (const auto& command : step.commands) {
                if ((command.wait == Wait::observed) != (is_observation(command.operation))) { return false; }
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (definition.steps[j].name == step.name) { return false; }
            }
        }
        if (definition.receipts.size() > kMaxSteps*kMaxCommands) { return false; }
        for (std::size_t i=0;i<definition.receipts.size();++i) {
            const auto& receipt=definition.receipts[i];
            if(receipt.name.empty() || receipt.step>=definition.steps.size()
                || receipt.command>=definition.steps[receipt.step].commands.size()
                || definition.steps[receipt.step].commands[receipt.command].wait==Wait::requested) { return false; }
            for(std::size_t j=0;j<i;++j) {
                const auto& prior=definition.receipts[j];
                if(prior.name==receipt.name || (prior.step==receipt.step && prior.command==receipt.command)) { return false; }
            }
        }
        return true;
    }

    [[nodiscard]] bool start(const Definition& definition, std::uint64_t run) noexcept {
        if (ownsCommands_ || phase_ == Phase::running) { return false; }
        if (!valid(definition) || run == 0 || incarnation_ == UINT64_MAX) {
            phase_ = Phase::failed; failure_ = Failure::definition; return false;
        }
        definition_ = &definition;
        run_ = run;
        ++incarnation_; // kept across cancel/restart, including a reused run id
        states_ = {};
        head_ = count_ = 0;
        complete_ = 0;
        rejected_ = duplicates_ = 0;
        failure_ = Failure::none;
        overflow_ = false;
        phase_ = Phase::running;
        return true;
    }

    [[nodiscard]] Token token(std::size_t step, std::size_t command) const noexcept {
        return {run_, incarnation_, static_cast<std::uint8_t>(step), static_cast<std::uint8_t>(command)};
    }
    [[nodiscard]] Token token(std::string_view receipt) const noexcept {
        if(definition_) {
            for(const auto& binding:definition_->receipts) {
                if(binding.name==receipt) { return token(binding.step,binding.command); }
            }
        }
        return {run_,incarnation_,UINT8_MAX,UINT8_MAX};
    }
    [[nodiscard]] const StepState& receipt_state(std::string_view receipt) const noexcept {
        const auto value=token(receipt);
        static constexpr StepState missing{};
        return value.step<states_.size()?states_[value.step]:missing;
    }
    [[nodiscard]] bool enqueue(Event event) noexcept {
        if (phase_ != Phase::running || !current(event.token)) { ++rejected_; return false; }
        if (count_ == queue_.size()) { overflow_ = true; return false; }
        queue_[(head_ + count_) % queue_.size()] = event;
        ++count_;
        return true;
    }

    void update(Services& services) noexcept {
        if (phase_ != Phase::running) { return; }
        if (overflow_) { fail(Failure::queueOverflow, services); return; }
        activate(services);
        while (count_ != 0 && phase_ == Phase::running) {
            const auto event = queue_[head_];
            head_ = (head_ + 1) % queue_.size();
            --count_;
            if (!current(event.token)) { ++rejected_; continue; }
            auto& state = states_[event.token.step].commands[event.token.command];
            if (event.milestone == Milestone::failed) { fail(Failure::native, services); return; }
            const auto& spec = definition_->steps[event.token.step].commands[event.token.command];
            if ((event.milestone == Milestone::observed) != (is_observation(spec.operation))) {
                ++rejected_; continue;
            }
            bool& flag = event.milestone == Milestone::nativeReady ? state.ready : state.completed;
            if (flag) { ++duplicates_; continue; }
            if (event.milestone == Milestone::completed && !state.ready) { ++rejected_; continue; }
            flag = true;
        }
        if (phase_ != Phase::running) { return; }
        for (std::size_t i = 0; i < definition_->steps.size(); ++i) {
            auto& state = states_[i];
            if (state.phase != StepPhase::active) { continue; }
            bool joined = true;
            const auto& step = definition_->steps[i];
            for (std::size_t j = 0; j < step.commands.size(); ++j) {
                const auto& command = state.commands[j];
                const auto wait = step.commands[j].wait;
                joined &= wait == Wait::requested ? command.requested
                    : wait == Wait::nativeReady ? command.ready : command.completed;
            }
            if (joined) { state.phase = StepPhase::complete; complete_ |= std::uint32_t{1} << i; }
        }
        const auto all = definition_->steps.size() == 32 ? UINT32_MAX
            : (std::uint32_t{1} << definition_->steps.size()) - 1U;
        if (complete_ == all) { phase_ = Phase::complete; }
        else { activate(services); }
    }

    void cancel(Services& services) noexcept {
        retire(services);
        definition_ = nullptr;
        count_ = head_ = 0;
        overflow_ = false;
        if (phase_ != Phase::idle) { phase_ = Phase::cancelled; }
    }
    [[nodiscard]] Diagnostics diagnostics() const noexcept {
        std::uint32_t active{};
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (states_[i].phase == StepPhase::active) { active |= std::uint32_t{1} << i; }
        }
        return {phase_, failure_, run_, incarnation_, rejected_, duplicates_, active, complete_, count_};
    }
    [[nodiscard]] const StepState& step_state(std::size_t step) const noexcept { return states_[step]; }

private:
    [[nodiscard]] bool current(Token value) const noexcept {
        return definition_ && value.run == run_ && value.incarnation == incarnation_
            && value.step < definition_->steps.size()
            && value.command < definition_->steps[value.step].commands.size()
            && states_[value.step].phase == StepPhase::active
            && states_[value.step].commands[value.command].requested;
    }
    [[nodiscard]] Command command(std::size_t step, std::size_t index) const noexcept {
        return {token(step, index), definition_->schema, definition_->steps[step].commands[index]};
    }
    void activate(Services& services) noexcept {
        for (std::size_t i = 0; i < definition_->steps.size(); ++i) {
            auto& state = states_[i];
            const auto& step = definition_->steps[i];
            if (state.phase != StepPhase::pending || (step.dependencies & complete_) != step.dependencies) { continue; }
            state.phase = StepPhase::active;
            for (std::size_t j = 0; j < step.commands.size(); ++j) {
                if (!services.publish(command(i, j))) { fail(Failure::publication, services); return; }
                state.commands[j].requested = true;
                ownsCommands_ = true;
            }
        }
    }
    void retire(Services& services) noexcept {
        if (!definition_) { return; }
        for (std::size_t i = definition_->steps.size(); i-- > 0;) {
            auto& state = states_[i];
            for (std::size_t j = definition_->steps[i].commands.size(); j-- > 0;) {
                auto& item = state.commands[j];
                if (item.requested && !item.retired) { services.cancel(command(i, j)); item.retired = true; }
            }
            if (state.phase == StepPhase::active) { state.phase = StepPhase::cancelled; }
        }
        ownsCommands_ = false;
    }
    void fail(Failure reason, Services& services) noexcept {
        failure_ = reason; phase_ = Phase::failed; count_ = head_ = 0; retire(services); definition_ = nullptr;
    }
    const Definition* definition_{};
    std::array<StepState, kMaxSteps> states_{};
    std::array<Event, kQueueSize> queue_{};
    std::uint64_t run_{}, incarnation_{}, rejected_{}, duplicates_{};
    std::size_t head_{}, count_{};
    std::uint32_t complete_{};
    Phase phase_{};
    Failure failure_{};
    bool ownsCommands_{}, overflow_{};
};
} // namespace dawn::state::activity::coo
