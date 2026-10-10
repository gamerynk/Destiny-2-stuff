#include "vendor_expression.h"

#include <array>

namespace dawn::state::build_data::vendors {
namespace {

enum class Kind : std::uint8_t {
    boolean,
    number,
};

struct Slot {
    Kind kind{};
    bool boolean{};
    std::int32_t number{};
};

class Stack final {
public:
    [[nodiscard]] bool push(const Slot& slot) noexcept {
        if (depth_ == slots_.size()) {
            return false;
        }
        slots_[depth_++] = slot;
        return true;
    }

    [[nodiscard]] bool pop(Kind kind, Slot& slot) noexcept {
        if (depth_ == 0 || slots_[depth_ - 1].kind != kind) {
            return false;
        }
        slot = slots_[--depth_];
        return true;
    }

    [[nodiscard]] std::size_t depth() const noexcept {
        return depth_;
    }

private:
    std::array<Slot, kExpressionStackCapacity> slots_{};
    std::size_t depth_{};
};

[[nodiscard]] Slot boolean_slot(bool value) noexcept {
    return {Kind::boolean, value, 0};
}

[[nodiscard]] Slot number_slot(std::int32_t value) noexcept {
    return {Kind::number, false, value};
}

[[nodiscard]] bool
step(const Instruction& instruction, const Inputs& inputs, Stack& stack) noexcept {
    Slot left{};
    Slot right{};
    switch (instruction.opcode) {
    case Opcode::flag: {
        std::uint8_t logical = 0;
        return inputs.flag(inputs.context, instruction.operand, logical)
               && stack.push(boolean_slot(logical == kFlagActive));
    }
    case Opcode::logicalNot:
        return stack.pop(Kind::boolean, left) && stack.push(boolean_slot(!left.boolean));
    case Opcode::loadValue: {
        std::int32_t value = 0;
        return inputs.value(inputs.context, instruction.operand, value)
               && stack.push(number_slot(value));
    }
    case Opcode::constant:
        return stack.push(number_slot(instruction.operand));
    case Opcode::lessThan:
        // the right operand was pushed last, so it comes off first
        return stack.pop(Kind::number, right) && stack.pop(Kind::number, left)
               && stack.push(boolean_slot(left.number < right.number));
    default:
        return false;
    }
}

} // namespace

bool evaluate(std::span<const Instruction> program, const Inputs& inputs, bool& result) noexcept {
    result = false;
    if (program.empty() || inputs.flag == nullptr || inputs.value == nullptr) {
        return false;
    }
    Stack stack;
    for (const Instruction& instruction : program) {
        if (!step(instruction, inputs, stack)) {
            return false;
        }
    }
    Slot final{};
    if (stack.depth() != 1 || !stack.pop(Kind::boolean, final)) {
        return false;
    }
    result = final.boolean;
    return true;
}

} // namespace dawn::state::build_data::vendors
