#include "ui_module_descriptor.h"

#include <algorithm>

namespace dawn::core::ui::modules {
namespace {

[[nodiscard]] bool is_valid_owner(Owner owner) noexcept {
    return owner == Owner::client || owner == Owner::server || owner == Owner::core;
}

[[nodiscard]] bool is_stable_id_byte(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '.'
           || value == '_';
}

// the ID is saved, so its alphabet must stay fixed
[[nodiscard]] bool is_valid_stable_id(std::string_view stableId) noexcept {
    if (stableId.empty() || stableId.size() > kStableIdCapacity) {
        return false;
    }
    if (stableId.front() < 'a' || stableId.front() > 'z') {
        return false;
    }
    return std::all_of(stableId.begin(), stableId.end(), is_stable_id_byte);
}

[[nodiscard]] bool is_valid_display_name(std::string_view displayName) noexcept {
    return !displayName.empty() && displayName.size() <= kDisplayNameCapacity
           && displayName.find('\0') == std::string_view::npos;
}

} // namespace

Owner Descriptor::owner() const noexcept {
    return owner_;
}

std::string_view Descriptor::stable_id() const noexcept {
    return {stableId_.data(), stableIdLength_};
}

std::string_view Descriptor::display_name() const noexcept {
    return {displayName_.data(), displayNameLength_};
}

FrameCallback Descriptor::frame_callback() const noexcept {
    return frameCallback_;
}

bool create_descriptor(Owner owner,
                       std::string_view stableId,
                       std::string_view displayName,
                       FrameCallback frameCallback,
                       Descriptor& output) noexcept {
    if (!is_valid_owner(owner) || !is_valid_stable_id(stableId)
        || !is_valid_display_name(displayName) || frameCallback == nullptr) {
        return false;
    }

    Descriptor candidate;
    candidate.owner_ = owner;
    std::copy(stableId.begin(), stableId.end(), candidate.stableId_.begin());
    candidate.stableIdLength_ = stableId.size();
    std::copy(displayName.begin(), displayName.end(), candidate.displayName_.begin());
    candidate.displayNameLength_ = displayName.size();
    candidate.frameCallback_ = frameCallback;
    output = candidate;
    return true;
}

bool is_valid(const Descriptor& descriptor) noexcept {
    return is_valid_owner(descriptor.owner_) && is_valid_stable_id(descriptor.stable_id())
           && is_valid_display_name(descriptor.display_name())
           && descriptor.frameCallback_ != nullptr;
}

} // namespace dawn::core::ui::modules
