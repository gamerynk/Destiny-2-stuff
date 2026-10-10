#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "parameter_registry.h"

namespace dawn::middleware::gameplay::group {

inline constexpr std::uint8_t kParameterUpdateId = 38;
inline constexpr std::uint8_t kParameterRequestId = 39;
inline constexpr std::uint32_t kParameterUpdateSize = 44064;
inline constexpr std::uint32_t kParameterRequestSize = 44056;
inline constexpr std::uint8_t kParameterCount = 25;
inline constexpr std::size_t kParameterMemberCapacity = 12;
inline constexpr std::size_t kEncodedParameterBodyCapacity = 1024;
inline constexpr std::uint8_t kUnspecifiedParameterMember = 0xFF;

// bodies after this are not decoded; each parameter has its own codec of unknown width
struct ParameterRequestHeader {
    std::uint64_t sessionId{};
    std::uint64_t requestedMask{};
    bool modeFlag{};
};

[[nodiscard]] bool read_parameter_request(encoding::bits::Reader& reader,
                                          ParameterRequestHeader& output) noexcept;

[[nodiscard]] bool requests(std::uint64_t mask, std::uint8_t parameter) noexcept;

// the authority walks the PC 1.59 schema only to find the boundary, then keeps the original
// bits and places them in the authenticated member's update-array slot
struct EncodedParameterBody {
    std::array<std::byte, kEncodedParameterBodyCapacity> bytes{};
    std::size_t bitCount{};
    std::size_t memberIndexBitOffset{};
    std::uint8_t memberIndex{kUnspecifiedParameterMember};
    bool memberIndexPresent{};
    bool present{};
};

[[nodiscard]] bool read_activity_selection_proposal(encoding::bits::Reader& reader,
                                                    EncodedParameterBody& output,
                                                    std::size_t* failureConsumedBits = nullptr) noexcept;

[[nodiscard]] bool read_activity_selection_responses_proposal(
    encoding::bits::Reader& reader,
    EncodedParameterBody& output,
    std::size_t* failureConsumedBits = nullptr) noexcept;

// absent root, absent optional field or native -1 is rebound to the authenticated row;
// a concrete index must name that exact row
[[nodiscard]] bool validate_parameter_proposal_member(const EncodedParameterBody& body,
                                                      std::uint8_t memberIndex) noexcept;

[[nodiscard]] bool same_parameter_body(const EncodedParameterBody& left,
                                       const EncodedParameterBody& right) noexcept;

inline constexpr std::uint64_t kEncodableParameters =
    (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::worldControllerGoalData))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::activityHost))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::activitySelection))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::activitySelectionResponses))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::currentActivity))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::previousActivity))
    | (std::uint64_t{1} << static_cast<std::uint8_t>(Parameter::publicSessionReservations));

struct WorldControllerGoalDataParameter {
    // setup:orbit is world-controller step 29
    std::int32_t goalStep{29};
    std::int32_t goalReason{};
    std::uint8_t acceptedSequence{1};
};

struct CurrentActivityParameter {
    bool present{};
    std::int8_t reason{};
    std::int16_t sourceActivityIndex{};
    std::int16_t activityIndex{};
    std::uint8_t fixedValue{2};
};

using PreviousActivityParameter = CurrentActivityParameter;

// the peer builds no activity client while this parameter holds no value
struct ActivityHostParameter {
    std::uint64_t selectionId{};
    std::uint64_t hostId{};
    std::uint32_t memberMask{};
    std::uint32_t address{};
    std::uint16_t port{};
};

// a joining peer needs one applied to finish its join, whatever the update names
struct ParameterUpdate {
    std::uint64_t sessionId{};
    bool resetFlag{};
    std::uint64_t releasedMask{};
    std::uint64_t carriedMask{};
    WorldControllerGoalDataParameter worldControllerGoalData{};
    ActivityHostParameter activityHost{};
    CurrentActivityParameter currentActivity{};
    PreviousActivityParameter previousActivity{};
    std::span<const EncodedParameterBody* const> activitySelections{};
    // a null or non-present entry emits the neutral child root (00); this array has no
    // element-presence wrapper
    std::span<const EncodedParameterBody* const> activitySelectionResponses{};
};

[[nodiscard]] bool write_parameter_update(encoding::bits::Writer& writer,
                                          const ParameterUpdate& body) noexcept;

} // namespace dawn::middleware::gameplay::group
