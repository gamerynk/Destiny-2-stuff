#pragma once

#include "../../protobuf/codec.h"

namespace dawn::middleware::bap::matchmaking::configuration_policy {

// PC 86657 configuration request key: activity and resolved activity TYPE hash. F1FC10
// copies the activity record hash, F1FC3A copies DDF190's result. 0x8132069D is a tag ref
inline constexpr std::uint32_t kTowerActivity = 0xE8ABA41BU;
inline constexpr std::uint32_t kTowerActivityType = 0x5EC025C8U;

// config requests don't carry search's root f4/f5; the key is root f11 { f1 activity_hash,
// f2 activity_type_hash, f3 optional extra hash }. Refuse ambiguous keys
[[nodiscard]] inline bool tower(std::span<const std::byte> body) noexcept {
    protobuf::Reader root(body);
    protobuf::Field field{};
    bool keySeen = false, activitySeen = false, activityTypeSeen = false;
    std::uint32_t activity = 0, activityType = 0;
    while (root.remaining()) {
        if (!root.next(field)) return false;
        if (field.fieldNumber != 11) continue;
        if (keySeen || field.wireType != protobuf::WireType::lengthDelimited) return false;
        keySeen = true;
        protobuf::Reader key(field.bytes);
        protobuf::Field value{};
        while (key.remaining()) {
            if (!key.next(value)) return false;
            if (value.fieldNumber != 1 && value.fieldNumber != 2) continue;
            if (value.wireType != protobuf::WireType::varint || value.value > 0xFFFFFFFFULL) return false;
            bool& seen = value.fieldNumber == 1 ? activitySeen : activityTypeSeen;
            if (seen) return false;
            seen = true;
            (value.fieldNumber == 1 ? activity : activityType) = static_cast<std::uint32_t>(value.value);
        }
    }
    return keySeen && activitySeen && activityTypeSeen
        && activity == kTowerActivity && activityType == kTowerActivityType;
}

} // namespace dawn::middleware::bap::matchmaking::configuration_policy
