#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace dawn::server::bap::remote_appearance {
// public family-0 appearance only; never broadcast private inventory families
struct Published {
    std::uint64_t root{},character{};
    std::int32_t version{};
    bool valid{};
};
struct Entry {
    Published state{};
    std::uint64_t readyAt{};
    bool dirty{};
};
struct Tracker {
    std::array<Entry,32> entries{};
    Entry* find(std::uint64_t root) noexcept {
        if(!root)return nullptr;
        for(auto& entry:entries)if(entry.state.valid && entry.state.root==root)return &entry;
        return nullptr;
    }
    bool full(Published state,std::uint64_t readyAt) noexcept {
        if(!state.valid || !state.root || !state.character || state.version!=0)return false;
        auto* entry=find(state.root);
        if(!entry)for(auto& candidate:entries)if(!candidate.state.valid){entry=&candidate;break;}
        if(!entry)return false;
        *entry={state,readyAt,false};return true;
    }
    void unsubscribe(std::uint64_t root) noexcept {
        if(auto* entry=find(root))*entry={};
    }
    void changed(std::uint64_t sourceAccount) noexcept {
        if(auto* entry=find(sourceAccount))entry->dirty=true;
    }
    static bool due(const Entry& entry,std::uint64_t now) noexcept {
        return entry.state.valid && entry.dirty && now>=entry.readyAt
            && entry.state.version<(std::numeric_limits<std::int32_t>::max)();
    }
    bool incremental(Published before,Published after) noexcept {
        auto* entry=find(before.root);
        if(!entry || !entry->dirty || !before.valid || !after.valid
            || entry->state.character!=before.character || entry->state.version!=before.version
            || after.root!=before.root || !after.character || before.version<0
            || before.version==(std::numeric_limits<std::int32_t>::max)()
            || after.version!=before.version+1)return false;
        entry->state=after;entry->dirty=false;return true;
    }
};
}
