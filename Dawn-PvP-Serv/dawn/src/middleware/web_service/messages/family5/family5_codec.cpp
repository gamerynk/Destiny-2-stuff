#include "family5_codec.h"

#include <limits>

namespace dawn::middleware::web_service::messages::family5 {
namespace {

constexpr std::uint8_t kPresenceWidth = 1;
constexpr std::size_t kNestedFieldCount = 10;
constexpr std::uint8_t kSoidWidth = 64;
constexpr std::uint8_t kOverrideCountWidth = 7;
constexpr std::uint8_t kOverrideSlotWidth = 16;
constexpr std::uint32_t kOverrideSlotBias = 0x8000;
constexpr std::uint8_t kFlagValueWidth = 2;
constexpr std::uint8_t kFlagValueBias = 1;
// last flag slot the client's state buffer holds; it uses slots as raw offsets unchecked, so drop the row
constexpr std::uint16_t kMaximumFlagSlot = 23499;
constexpr std::uint16_t kMaximumValueSlot = 15499;
constexpr std::uint8_t kMaximumFlagValue = 2;
constexpr std::uint8_t kUnlockValueWidth = 32;
constexpr std::uint8_t kContentGateArmWidth = 32;
constexpr std::uint32_t kContentGateArmValue = 1;

enum class NestedField : std::size_t {
    objectSoid = 0,
    flagOverrides = 4,
    valueOverrides = 5,
    contentGateArm = 7,
};

[[nodiscard]] std::size_t safe_flag_count(const state::Family5State& family) noexcept {
    std::size_t safe = 0;
    for (std::size_t index = 0; index < family.flagCount; ++index) {
        const auto& row = family.flags[index];
        if (row.slot <= kMaximumFlagSlot && row.value <= kMaximumFlagValue) {
            ++safe;
        }
    }
    return safe;
}

[[nodiscard]] std::size_t safe_value_count(const state::Family5State& family) noexcept {
    std::size_t safe = 0;
    for (std::size_t index = 0; index < family.valueCount; ++index) {
        if (family.values[index].slot <= kMaximumValueSlot) {
            ++safe;
        }
    }
    return safe;
}

[[nodiscard]] bool write_flags(encoding::bits::Writer& writer,
                               const state::Family5State& family) noexcept {
    bool encoded = writer.write(safe_flag_count(family), kOverrideCountWidth);
    for (std::size_t index = 0; encoded && index < family.flagCount; ++index) {
        const auto& row = family.flags[index];
        if (row.slot > kMaximumFlagSlot || row.value > kMaximumFlagValue) {
            continue;
        }
        const std::uint32_t storedSlot = row.slot + kOverrideSlotBias;
        const std::uint32_t storedValue = row.value + kFlagValueBias;
        encoded = writer.write(1U, kPresenceWidth) && writer.write(storedSlot, kOverrideSlotWidth)
                  && writer.write(1U, kPresenceWidth) && writer.write(storedValue, kFlagValueWidth);
    }
    return encoded;
}

[[nodiscard]] bool write_values(encoding::bits::Writer& writer,
                                const state::Family5State& family) noexcept {
    bool encoded = writer.write(safe_value_count(family), kOverrideCountWidth);
    for (std::size_t index = 0; encoded && index < family.valueCount; ++index) {
        const auto& row = family.values[index];
        if (row.slot > kMaximumValueSlot) {
            continue;
        }
        const std::uint32_t storedSlot = row.slot + kOverrideSlotBias;
        const std::int64_t biased =
            static_cast<std::int64_t>(row.value) - (std::numeric_limits<std::int32_t>::min)();
        encoded = writer.write(1U, kPresenceWidth) && writer.write(storedSlot, kOverrideSlotWidth)
                  && writer.write(1U, kPresenceWidth)
                  && writer.write(static_cast<std::uint32_t>(biased), kUnlockValueWidth);
    }
    return encoded;
}

} // namespace

bool valid(const state::Family5State& family) noexcept {
    return family.flagCount <= family.flags.size() && family.valueCount <= family.values.size();
}

bool write(encoding::bits::Writer& writer, const state::Family5State& family) noexcept {
    bool encoded = true;
    for (std::size_t index = 0; encoded && index < kNestedFieldCount; ++index) {
        const auto field = static_cast<NestedField>(index);
        if (field == NestedField::objectSoid) {
            encoded =
                writer.write(1U, kPresenceWidth) && writer.write(family.objectSoid, kSoidWidth);
        } else if (field == NestedField::flagOverrides && family.flagCount != 0) {
            // an empty list stays absent; a present zero count is a different wire shape
            encoded = writer.write(1U, kPresenceWidth) && write_flags(writer, family);
        } else if (field == NestedField::valueOverrides && family.valueCount != 0) {
            encoded = writer.write(1U, kPresenceWidth) && write_values(writer, family);
        } else if (field == NestedField::contentGateArm && family.contentGateArm) {
            encoded = writer.write(1U, kPresenceWidth)
                      && writer.write(kContentGateArmValue, kContentGateArmWidth);
        } else {
            encoded = writer.write(0U, kPresenceWidth);
        }
    }
    return encoded;
}

} // namespace dawn::middleware::web_service::messages::family5
