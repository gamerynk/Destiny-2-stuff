#pragma once
#include "executor.h"

namespace dawn::state::activity::coo {
struct ServiceContext final {
    const Definition& definition;
    std::uint64_t run, incarnation;
};

template<class Binding>
class NativeServices : public Services {
public:
    [[nodiscard]] bool publish(const Command& command) noexcept final {
        return matches(command) && binding().request(command);
    }
    void cancel(const Command& command) noexcept final {
        if (matches(command)) { binding().retire(command); }
    }
private:
    [[nodiscard]] Binding& binding() noexcept { return static_cast<Binding&>(*this); }
    [[nodiscard]] bool matches(const Command& command) noexcept {
        const auto context = binding().context();
        if (context.run == 0 || command.token.run != context.run
            || command.token.incarnation != context.incarnation
            || command.schema != context.definition.schema
            || command.token.step >= context.definition.steps.size()) { return false; }
        const auto commands = context.definition.steps[command.token.step].commands;
        if (command.token.command >= commands.size()) { return false; }
        const auto& expected = commands[command.token.command];
        return command.spec.operation == expected.operation && command.spec.asset == expected.asset
            && command.spec.argument == expected.argument && command.spec.wait == expected.wait;
    }
};
} // namespace dawn::state::activity::coo
