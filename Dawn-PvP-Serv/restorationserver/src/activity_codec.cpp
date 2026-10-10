#include "activity_codec.h"

#include <algorithm>
#include <type_traits>

#include "state/activity/destination/activity_destination_validation.h"

namespace restoration::activity_codec {
namespace {

using namespace dawn::state::activity;
constexpr std::uint32_t kMagic = 0x31544341U; // "ACT1" little endian

class Writer final {
public:
    explicit Writer(std::vector<std::byte>& output) noexcept : output_(output) {
        output_.clear();
    }

    [[nodiscard]] bool byte(std::uint8_t value) noexcept {
        try {
            output_.push_back(static_cast<std::byte>(value));
            return true;
        } catch (...) {
            return false;
        }
    }

    template <typename Value>
    [[nodiscard]] bool integer(Value value) noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        Unsigned bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            if (!byte(static_cast<std::uint8_t>(bits & static_cast<Unsigned>(0xFFU)))) {
                return false;
            }
            bits >>= 8U;
        }
        return true;
    }

    [[nodiscard]] bool boolean(bool value) noexcept { return byte(value ? 1U : 0U); }

    template <std::size_t Size>
    [[nodiscard]] bool bytes(const std::array<std::byte, Size>& values) noexcept {
        for (const std::byte value : values) {
            if (!byte(std::to_integer<std::uint8_t>(value))) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<std::byte>& output_;
};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> input) noexcept : input_(input) {}

    template <typename Value>
    [[nodiscard]] bool integer(Value& value) noexcept {
        static_assert(std::is_integral_v<Value>);
        if (offset_ > input_.size() || input_.size() - offset_ < sizeof(Value)) {
            return false;
        }
        using Unsigned = std::make_unsigned_t<Value>;
        Unsigned bits = 0;
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bits |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(input_[offset_++]))
                    << static_cast<unsigned>(index * 8U);
        }
        value = static_cast<Value>(bits);
        return true;
    }

    [[nodiscard]] bool boolean(bool& value) noexcept {
        std::uint8_t raw = 0;
        if (!integer(raw) || raw > 1U) {
            return false;
        }
        value = raw != 0;
        return true;
    }

    template <std::size_t Size>
    [[nodiscard]] bool bytes(std::array<std::byte, Size>& values) noexcept {
        for (std::byte& value : values) {
            std::uint8_t raw = 0;
            if (!integer(raw)) {
                return false;
            }
            value = static_cast<std::byte>(raw);
        }
        return true;
    }

    [[nodiscard]] bool finished() const noexcept { return offset_ == input_.size(); }

private:
    std::span<const std::byte> input_;
    std::size_t offset_{};
};

[[nodiscard]] bool write_destination(Writer& writer,
                                     const destination::DestinationSelection& value) noexcept {
    for (const std::int8_t byte : value.packageName) {
        if (!writer.integer(byte)) {
            return false;
        }
    }
    return writer.integer(value.packageNameLength) && writer.integer(value.reason)
           && writer.integer(value.previousActivityIndex) && writer.integer(value.activityIndex)
           && writer.integer(value.elementIndex) && writer.integer(value.arrivalBubbleHash)
           && writer.integer(value.spawnSetHash) && writer.boolean(value.hasElementIndex)
           && writer.boolean(value.hasArrivalBubbleHash) && writer.boolean(value.hasSpawnSetHash)
           && writer.integer(value.arrivalBubbleOverride)
           && writer.boolean(value.hasArrivalBubbleOverride)
           && writer.integer(value.sliceSetOverride)
           && writer.boolean(value.hasSliceSetOverride)
           && writer.integer(value.spawnSetOverride)
           && writer.boolean(value.hasSpawnSetOverride) && writer.bytes(value.descriptorBits)
           && writer.integer(value.descriptorBitLength)
           && writer.integer(value.descriptorNameBit) && writer.boolean(value.hasDescriptorName);
}

[[nodiscard]] bool read_destination(Reader& reader,
                                    destination::DestinationSelection& value) noexcept {
    for (std::int8_t& byte : value.packageName) {
        if (!reader.integer(byte)) {
            return false;
        }
    }
    return reader.integer(value.packageNameLength) && reader.integer(value.reason)
           && reader.integer(value.previousActivityIndex) && reader.integer(value.activityIndex)
           && reader.integer(value.elementIndex) && reader.integer(value.arrivalBubbleHash)
           && reader.integer(value.spawnSetHash) && reader.boolean(value.hasElementIndex)
           && reader.boolean(value.hasArrivalBubbleHash) && reader.boolean(value.hasSpawnSetHash)
           && reader.integer(value.arrivalBubbleOverride)
           && reader.boolean(value.hasArrivalBubbleOverride)
           && reader.integer(value.sliceSetOverride)
           && reader.boolean(value.hasSliceSetOverride)
           && reader.integer(value.spawnSetOverride)
           && reader.boolean(value.hasSpawnSetOverride) && reader.bytes(value.descriptorBits)
           && reader.integer(value.descriptorBitLength)
           && reader.integer(value.descriptorNameBit) && reader.boolean(value.hasDescriptorName);
}

[[nodiscard]] bool write_membership(Writer& writer,
                                    const membership::MembershipState& value) noexcept {
    const auto& identity = value.identity;
    const auto& spawn = value.spawn;
    const auto& teleport = value.teleport;
    const auto& region = value.region;
    return writer.integer(identity.memberKey) && writer.integer(identity.smallOpaque)
           && writer.integer(identity.signedOpaque) && writer.integer(identity.joinIdentity)
           && writer.integer(identity.accountSoid) && writer.integer(identity.opaqueSoid)
           && writer.integer(identity.secondaryOpaque) && writer.integer(spawn.state)
           && writer.integer(spawn.opaqueByte) && writer.integer(spawn.opaqueValue)
           && writer.integer(teleport.state) && writer.integer(teleport.token)
           && writer.integer(teleport.sliceSetIndex) && writer.integer(teleport.sliceSetHash)
           && writer.integer(region.index) && writer.integer(region.hash)
           && writer.integer(value.revision) && writer.integer(value.acknowledgedRevision)
           && writer.integer(value.transitionToken) && writer.boolean(value.hasTransitionToken)
           && writer.boolean(value.hasIdentity);
}

[[nodiscard]] bool read_membership(Reader& reader,
                                   membership::MembershipState& value) noexcept {
    auto& identity = value.identity;
    auto& spawn = value.spawn;
    auto& teleport = value.teleport;
    auto& region = value.region;
    return reader.integer(identity.memberKey) && reader.integer(identity.smallOpaque)
           && reader.integer(identity.signedOpaque) && reader.integer(identity.joinIdentity)
           && reader.integer(identity.accountSoid) && reader.integer(identity.opaqueSoid)
           && reader.integer(identity.secondaryOpaque) && reader.integer(spawn.state)
           && reader.integer(spawn.opaqueByte) && reader.integer(spawn.opaqueValue)
           && reader.integer(teleport.state) && reader.integer(teleport.token)
           && reader.integer(teleport.sliceSetIndex) && reader.integer(teleport.sliceSetHash)
           && reader.integer(region.index) && reader.integer(region.hash)
           && reader.integer(value.revision) && reader.integer(value.acknowledgedRevision)
           && reader.integer(value.transitionToken) && reader.boolean(value.hasTransitionToken)
           && reader.boolean(value.hasIdentity);
}

[[nodiscard]] bool valid(const ActivityState& value, std::uint64_t ownerAccountSoid) noexcept {
    if (ownerAccountSoid == 0 || value.stateRevision == kInvalidRevision
        || value.nextSessionId == kAbsentSessionId
        || value.allocatorRevision == kInvalidRevision) {
        return false;
    }
    for (std::size_t index = 0; index < value.sessions.size(); ++index) {
        const SessionRecord& record = value.sessions[index];
        if (!record.occupied) {
            continue;
        }
        if (record.sessionId == kAbsentSessionId || record.ownerAccountSoid != ownerAccountSoid
            || record.createdRevision == kInvalidRevision
            || record.recordRevision == kInvalidRevision
            || record.createdRevision > record.recordRevision
            || record.recordRevision > value.stateRevision
            || !destination::valid(record.destination)
            || record.destination.descriptorBitLength
                   > record.destination.descriptorBits.size() * 8U
            || record.membership.teleport.state < membership::kMinimumMirroredState
            || record.membership.teleport.state > membership::kMaximumMirroredState
            || record.membership.teleport.sliceSetIndex < membership::kAbsentSliceSetIndex
            || record.membership.teleport.sliceSetIndex > membership::kMaximumSliceSetIndex
            || record.membership.region.index < membership::kAbsentRegionIndex
            || record.membership.region.index > membership::kMaximumRegionIndex
            || (record.joined
                != (record.joinedRevision != kInvalidRevision))) {
            return false;
        }
        if (record.membership.hasIdentity
            && (record.membership.revision == membership::kAbsentRevision
                || record.membership.identity.memberKey != record.memberKey)) {
            return false;
        }
        for (std::size_t other = index + 1; other < value.sessions.size(); ++other) {
            if (value.sessions[other].occupied
                && value.sessions[other].sessionId == record.sessionId) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool encode(const ActivityState& activity, std::vector<std::byte>& output) noexcept {
    if (!valid(activity,
               [&]() noexcept {
                   for (const SessionRecord& record : activity.sessions) {
                       if (record.occupied) {
                           return record.ownerAccountSoid;
                       }
                   }
                   return std::uint64_t{1};
               }())) {
        return false;
    }
    Writer writer(output);
    std::uint8_t occupied = 0;
    for (const SessionRecord& record : activity.sessions) {
        occupied += static_cast<std::uint8_t>(record.occupied);
    }
    if (!writer.integer(kMagic) || !writer.integer(activity.stateRevision)
        || !writer.integer(activity.nextSessionId) || !writer.integer(activity.allocatorRevision)
        || !writer.boolean(activity.allocatorExhausted) || !writer.integer(occupied)) {
        return false;
    }
    for (std::size_t slot = 0; slot < activity.sessions.size(); ++slot) {
        const SessionRecord& record = activity.sessions[slot];
        if (!record.occupied) {
            continue;
        }
        if (!writer.integer(static_cast<std::uint8_t>(slot))
            || !write_destination(writer, record.destination)
            || !writer.bytes(record.heldEntitySlots) || !writer.bytes(record.serverEntitySlots)
            || !write_membership(writer, record.membership)) {
            return false;
        }
        for (const std::uint16_t token : record.bubbleAuthority.grantTokens) {
            if (!writer.integer(token)) {
                return false;
            }
        }
        if (!writer.integer(record.sessionId) || !writer.integer(record.ownerAccountSoid)
            || !writer.integer(record.memberKey) || !writer.integer(record.createdRevision)
            || !writer.integer(record.recordRevision) || !writer.integer(record.joinedRevision)
            || !writer.boolean(record.joined)) {
            return false;
        }
    }
    return true;
}

bool decode(std::span<const std::byte> input,
            std::uint64_t ownerAccountSoid,
            ActivityState& activity) noexcept {
    activity = {};
    Reader reader(input);
    std::uint32_t magic = 0;
    std::uint8_t occupied = 0;
    if (!reader.integer(magic) || magic != kMagic || !reader.integer(activity.stateRevision)
        || !reader.integer(activity.nextSessionId) || !reader.integer(activity.allocatorRevision)
        || !reader.boolean(activity.allocatorExhausted) || !reader.integer(occupied)
        || occupied > activity.sessions.size()) {
        activity = {};
        return false;
    }
    for (std::size_t count = 0; count < occupied; ++count) {
        std::uint8_t slot = 0;
        if (!reader.integer(slot) || slot >= activity.sessions.size()
            || activity.sessions[slot].occupied) {
            activity = {};
            return false;
        }
        SessionRecord& record = activity.sessions[slot];
        if (!read_destination(reader, record.destination) || !reader.bytes(record.heldEntitySlots)
            || !reader.bytes(record.serverEntitySlots)
            || !read_membership(reader, record.membership)) {
            activity = {};
            return false;
        }
        for (std::uint16_t& token : record.bubbleAuthority.grantTokens) {
            if (!reader.integer(token)) {
                activity = {};
                return false;
            }
        }
        if (!reader.integer(record.sessionId) || !reader.integer(record.ownerAccountSoid)
            || !reader.integer(record.memberKey) || !reader.integer(record.createdRevision)
            || !reader.integer(record.recordRevision) || !reader.integer(record.joinedRevision)
            || !reader.boolean(record.joined)) {
            activity = {};
            return false;
        }
        record.occupied = true;
        record.restoredDormant = true;
    }
    if (!reader.finished() || !valid(activity, ownerAccountSoid)) {
        activity = {};
        return false;
    }
    return true;
}

bool self_test(const ActivityState& activity, std::uint64_t ownerAccountSoid) noexcept {
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    ActivityState decoded{};
    return valid(activity, ownerAccountSoid) && encode(activity, first)
           && decode(first, ownerAccountSoid, decoded) && encode(decoded, second)
           && first == second;
}

} // namespace restoration::activity_codec
