#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::state::build_data::vendors {

inline constexpr std::size_t kExpressionStackCapacity = 16;

enum class Opcode : std::uint8_t {
    flag = 1,
    logicalNot = 2,
    loadValue = 10,
    constant = 11,
    lessThan = 15,
};

inline constexpr std::uint8_t kFlagActive = 2;

struct Instruction {
    Opcode opcode{};
    std::uint16_t operand{};
};

using FlagReader = bool (*)(void* context, std::uint16_t slot, std::uint8_t& logical) noexcept;
using ValueReader = bool (*)(void* context, std::uint16_t slot, std::int32_t& value) noexcept;

struct Inputs {
    FlagReader flag{};
    ValueReader value{};
    void* context{};
};

// every failure is refused, never defaulted: bad opcode, stack, kind or unreadable slot
[[nodiscard]] bool
evaluate(std::span<const Instruction> program, const Inputs& inputs, bool& result) noexcept;

} // namespace dawn::state::build_data::vendors
