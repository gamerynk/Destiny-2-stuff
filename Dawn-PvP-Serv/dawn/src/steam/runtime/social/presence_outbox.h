#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace dawn::steam::runtime::social::presence_outbox {

// Steam permits twenty local keys; deletion tombstones are kept separately so clearing
// all twenty does not block setting twenty replacements
inline constexpr std::size_t kKeys = 20;
struct Update final {
    std::array<char, 64> key{};
    std::array<char, 256> value{};
    std::uint64_t revision{};
    bool clear{};
};

class Outbox final {
    struct Entry final {
        Update update{};
        bool occupied{};
        bool dirty{};
    };
    std::array<Entry, kKeys * 2> entries_{};
    std::uint64_t nextRevision_{1};
    std::uint64_t clearRevision_{};

public:
    [[nodiscard]] bool set(std::string_view key, std::string_view value) noexcept {
        if (key.empty() || key.size() >= Update{}.key.size()
            || value.size() >= Update{}.value.size()
            || key.find('\0') != key.npos || value.find('\0') != value.npos) return false;
        Entry* found = nullptr;
        Entry* free = nullptr;
        std::size_t count = 0;
        for (auto& entry : entries_) {
            if (!entry.occupied) { if (!free) free = &entry; continue; }
            if (entry.update.value[0]) ++count;
            if (key == entry.update.key.data()) found = &entry;
        }
        if (!value.empty() && (!found || !found->update.value[0]) && count >= kKeys)
            return false;
        if (found && value == found->update.value.data()) return true;
        if (!found) found = free;
        if (!found) return false;
        *found = {};
        std::memcpy(found->update.key.data(), key.data(), key.size());
        if (!value.empty()) std::memcpy(found->update.value.data(), value.data(), value.size());
        found->update.revision = nextRevision_++;
        found->occupied = true;
        found->dirty = true;
        return true;
    }

    void clear() noexcept {
        entries_ = {};
        clearRevision_ = nextRevision_++;
    }

    [[nodiscard]] Update next() const noexcept {
        if (clearRevision_) { Update result{}; result.clear = true; result.revision = clearRevision_; return result; }
        const Entry* first = nullptr;
        for (const auto& entry : entries_)
            if (entry.occupied && entry.dirty
                && (!first || entry.update.revision < first->update.revision)) first = &entry;
        return first ? first->update : Update{};
    }

    void complete(const Update& sent, bool success) noexcept {
        if (!success || !sent.revision) return; // idempotent retry; never drop a failed write
        if (sent.clear) {
            if (clearRevision_ == sent.revision) clearRevision_ = 0;
            return;
        }
        for (auto& entry : entries_) {
            if (entry.occupied && entry.update.revision == sent.revision) {
                if (entry.update.value[0]) entry.dirty = false;
                else entry = {};
                return;
            }
        }
        // a newer value or clear superseded this in-flight write; its ack must not erase the
        // newer state
    }

    [[nodiscard]] bool pending() const noexcept { return next().revision != 0; }
};
} // namespace dawn::steam::runtime::social::presence_outbox
