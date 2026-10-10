#pragma once
#include "native_services.h"
#include <type_traits>

namespace dawn::state::activity::coo {
struct MissionInput final {
    std::uint64_t run{}, now{};
    int region{};
    bool entrance{}, executor{};
};
struct ModuleBinding final { Asset asset; std::uint32_t id; };
struct ObservationBinding final { std::uint8_t fact, step, command; };
struct MissionDefinition final {
    Definition sequence;
    std::span<const ModuleBinding> modules;
    std::span<const ObservationBinding> observations;
};

template<class Frame>
struct MissionPorts {
    virtual void update_module(std::uint32_t, const MissionInput&, Frame&) noexcept = 0;
    virtual std::uint32_t observations(std::uint64_t, const Frame&) noexcept = 0;
    virtual ~MissionPorts() = default;
};

class MissionRuntime final {
public:
    static constexpr std::size_t kMaxModules = 8;
    [[nodiscard]] static bool valid(const MissionDefinition& mission) noexcept {
        if (!Executor::valid(mission.sequence) || mission.modules.empty()
            || mission.modules.size() > kMaxModules || mission.observations.size() > 32) { return false; }
        std::uint32_t facts{};
        for (std::size_t i = 0; i < mission.modules.size(); ++i) {
            const auto& module = mission.modules[i];
            for (std::size_t j = 0; j < i; ++j) {
                if (module.id == mission.modules[j].id || module.asset == mission.modules[j].asset) { return false; }
            }
            unsigned requests{};
            for (const auto& step : mission.sequence.steps) for (const auto& command : step.commands) {
                if (command.operation == Operation::mechanic && command.asset == module.asset
                    && command.argument == module.id && command.wait == Wait::requested) { ++requests; }
            }
            if (requests != 1) { return false; }
        }
        for (const auto& observation : mission.observations) {
            if (observation.fact >= 32 || observation.step >= mission.sequence.steps.size()
                || observation.command >= mission.sequence.steps[observation.step].commands.size()
                || (facts & (1U << observation.fact)) != 0) { return false; }
            const auto& spec = mission.sequence.steps[observation.step].commands[observation.command];
            if (spec.operation != Operation::observation || spec.wait != Wait::observed) { return false; }
            for (const auto& prior : mission.observations) {
                if (&prior == &observation) { break; }
                if (prior.step == observation.step && prior.command == observation.command) { return false; }
            }
            facts |= 1U << observation.fact;
        }
        for (std::size_t i = 0; i < mission.sequence.steps.size(); ++i) {
            const auto commands = mission.sequence.steps[i].commands;
            for (std::size_t j = 0; j < commands.size(); ++j) {
                const auto& spec = commands[j];
                bool bound{};
                if (spec.operation == Operation::mechanic && spec.wait == Wait::requested) {
                    for (const auto& module : mission.modules) {
                        bound |= spec.asset == module.asset && spec.argument == module.id;
                    }
                } else if (spec.operation == Operation::observation) {
                    for (const auto& observation : mission.observations) {
                        bound |= observation.step == i && observation.command == j;
                    }
                }
                if (!bound) { return false; }
            }
        }
        return true;
    }
    [[nodiscard]] bool select(std::uint64_t run, bool requested) noexcept {
        if (run == 0) { return false; }
        if (selectionRun_ != run) { selectionRun_ = run; selected_ = requested; }
        return selected_;
    }
    template<class Frame>
    [[nodiscard]] Frame update(const MissionDefinition& mission, const MissionInput& input,
                               MissionPorts<Frame>& ports) noexcept {
        if (input.run == 0) { return {}; }
        static_cast<void>(select(input.run, input.executor));
        Driver driver(*this);
        if (run_ != input.run) {
            executor_.cancel(driver);
            leases_ = {}; observed_ = 0; failedDefinition_ = false;
            run_ = input.run; definition_ = &mission;
            started_ = valid(mission) && (!selected_ || executor_.start(mission.sequence, run_));
            failedDefinition_ = !started_;
        } else if (definition_ != &mission) {
            executor_.cancel(driver); started_ = false; failedDefinition_ = true;
        }
        if (!started_) { return {}; }
        if (selected_) {
            executor_.update(driver);
            if (executor_.diagnostics().phase == Phase::failed) { return {}; }
        }
        Frame frame{};
        auto selectedInput = input;
        selectedInput.executor = selected_;
        for (std::size_t i = 0; i < mission.modules.size(); ++i) {
            if (!selected_ || leases_[i]) { ports.update_module(mission.modules[i].id, selectedInput, frame); }
        }
        if (selected_ && executor_.diagnostics().phase == Phase::running) {
            observed_ |= ports.observations(input.run, frame);
            // latch facts across fast transitions and direct arrivals;
            // visit in authored order to keep publication timing
            for (const auto& observation : mission.observations) {
                if ((observed_ & (1U << observation.fact)) == 0
                    || executor_.step_state(observation.step).phase != StepPhase::active) { continue; }
                static_cast<void>(executor_.enqueue({executor_.token(observation.step, observation.command), Milestone::observed}));
                executor_.update(driver);
            }
        }
        return frame;
    }
    void reset() noexcept {
        Driver driver(*this); executor_.cancel(driver);
        definition_ = nullptr; run_ = selectionRun_ = 0;
        selected_ = started_ = failedDefinition_ = false; observed_ = 0; leases_ = {};
    }
    [[nodiscard]] bool selected() const noexcept { return selected_; }
    [[nodiscard]] Diagnostics diagnostics() const noexcept {
        auto result = executor_.diagnostics();
        if (failedDefinition_) { result.phase = Phase::failed; result.failure = Failure::definition; result.run = run_; }
        return result;
    }
private:
    class Driver final : public NativeServices<Driver> {
    public:
        explicit Driver(MissionRuntime& owner) noexcept : owner_(owner) {}
        [[nodiscard]] ServiceContext context() const noexcept {
            return {owner_.definition_->sequence, owner_.run_, owner_.executor_.diagnostics().incarnation};
        }
        bool request(const Command& command) noexcept {
            if (command.spec.operation == Operation::observation) { return true; }
            for (std::size_t i = 0; i < owner_.definition_->modules.size(); ++i) {
                const auto& module = owner_.definition_->modules[i];
                if (command.spec.asset == module.asset && command.spec.argument == module.id) {
                    owner_.leases_[i] = true; return true;
                }
            }
            return false;
        }
        void retire(const Command& command) noexcept {
            if (command.spec.operation != Operation::mechanic) { return; }
            for (std::size_t i = 0; i < owner_.definition_->modules.size(); ++i) {
                if (command.spec.argument == owner_.definition_->modules[i].id) { owner_.leases_[i] = false; }
            }
        }
    private:
        MissionRuntime& owner_;
    };
    Executor executor_{};
    const MissionDefinition* definition_{};
    std::uint64_t run_{}, selectionRun_{};
    std::array<bool, kMaxModules> leases_{};
    std::uint32_t observed_{};
    bool selected_{}, started_{}, failedDefinition_{};
};
static_assert(std::is_trivially_copyable_v<MissionRuntime>);
} // namespace dawn::state::activity::coo
