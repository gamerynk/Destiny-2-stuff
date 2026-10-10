#pragma once
#include "executor.h"

namespace dawn::state::activity::coo {
struct TraversalBinding final { Asset asset; std::uint8_t stage; };
struct ObjectiveBinding final { Asset asset; std::uint32_t event; };
struct DialogueBinding final { Asset asset; std::uint8_t row; std::uint32_t delayMs; };
struct PresentationBindings final {
    Schema schema;
    std::span<const TraversalBinding> traversal;
    std::span<const ObjectiveBinding> objectives;
    std::span<const DialogueBinding> dialogue;
};

template<class Target>
class PresentationServices final {
public:
    PresentationServices(const PresentationBindings& bindings, Target& target,
                         std::uint64_t now) noexcept : bindings_(bindings), target_(target), now_(now) {}
    [[nodiscard]] bool valid(const Command& command) const noexcept {
        if (command.schema != bindings_.schema || command.spec.wait != Wait::requested) { return false; }
        switch (command.spec.operation) {
        case Operation::traversal: return unique(bindings_.traversal, command.spec.asset) != nullptr;
        case Operation::objective: return unique(bindings_.objectives, command.spec.asset) != nullptr;
        case Operation::dialogue: return unique(bindings_.dialogue, command.spec.asset) != nullptr;
        default: return false;
        }
    }
    [[nodiscard]] bool publish(const Command& command) noexcept {
        if (!valid(command)) { return false; }
        switch (command.spec.operation) {
        case Operation::traversal:
            target_.traverse(unique(bindings_.traversal, command.spec.asset)->stage); break;
        case Operation::objective:
            target_.set_objective(unique(bindings_.objectives, command.spec.asset)->event); break;
        case Operation::dialogue: {
            const auto& binding = *unique(bindings_.dialogue, command.spec.asset);
            target_.enqueue(binding.row, now_, binding.delayMs); break;
        }
        default: return false;
        }
        return true;
    }
private:
    template<class Binding>
    [[nodiscard]] static const Binding* unique(std::span<const Binding> bindings, Asset asset) noexcept {
        const Binding* match{};
        for (const auto& binding : bindings) {
            if (binding.asset != asset) { continue; }
            if (match) { return nullptr; }
            match = &binding;
        }
        return match;
    }
    const PresentationBindings& bindings_;
    Target& target_;
    std::uint64_t now_;
};
} // namespace dawn::state::activity::coo
