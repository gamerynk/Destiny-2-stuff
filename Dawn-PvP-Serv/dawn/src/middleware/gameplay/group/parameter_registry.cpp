#include "parameter_registry.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace dawn::middleware::gameplay::group {

namespace {

constexpr std::array<const char*, 25> kNames{
    "world-controller-goal-data",
    "active-join-controls",
    "atomic-cascade-join-data",
    "activity-host",
    "jip-gate",
    "activity-selection",
    "activity-selection-responses",
    "current-activity",
    "previous-activity",
    "user-join-controls",
    "desired-join-controls",
    "language",
    "requested-remote-join-data",
    "remote-join-data",
    "remote-join-result",
    "host-selected",
    "matchmaking-messaging",
    "matchmaking-abort-requested",
    "session-disband-reason",
    "matchmaking-progress",
    "initial-slice-set-status",
    "public-session-reservations",
    "matchmaking-data",
    "matchmaking-peer-data",
    "network-quality",
};

void append(char* output, std::size_t capacity, std::size_t& used, const char* text) noexcept {
    while (*text != '\0' && used + 1 < capacity) {
        output[used] = *text;
        ++used;
        ++text;
    }
}

} // namespace

const char* parameter_name(std::uint8_t index) noexcept {
    return index < kNames.size() ? kNames[index] : "unknown";
}

const char* parameter_names(std::uint64_t mask, char* output, std::size_t capacity) noexcept {
    if (output == nullptr || capacity == 0) {
        return "";
    }
    std::size_t used = 0;
    for (std::size_t index = 0; index < kNames.size(); ++index) {
        if (((mask >> index) & 1U) == 0) {
            continue;
        }
        if (used != 0) {
            append(output, capacity, used, ",");
        }
        append(output, capacity, used, kNames[index]);
    }
    if (used == 0) {
        append(output, capacity, used, "none");
    }
    output[used] = '\0';
    return output;
}

} // namespace dawn::middleware::gameplay::group
