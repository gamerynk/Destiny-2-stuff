#pragma once
#include "catalog.h"
#include "../../build_data/scenarios/definition.h"
#include "../../../middleware/bap/activity_message/sensor_auth_update.h"

namespace dawn::state::activity::io {
namespace roster {
namespace layouts=state::build_data::scenarios;
namespace wire=middleware::bap::activity_message::sensor_auth_update;
struct Storage final {
    std::array<layouts::RosterGroup,kRegistries.size()> authored{};
    std::array<std::array<std::uint32_t,wire::kBubbleKeyCapacity>,64> keys{};
    std::array<wire::BubbleSubBlock,64> blocks{};
};
[[nodiscard]] inline bool compose(const layouts::Definition& layout,
    std::span<const layouts::RosterGroup> cache,Storage& storage,wire::Roster& output,bool creator) noexcept {
    output={};
    if(layout.tag!=kDestination.scenario || layout.rosterGroupCount>layout.rosterGroups.size()
        || layout.bubbleGroupCount>layout.bubbleGroups.size())return false;
    wire::Roster next{};std::array<std::uint64_t,wire::kGroupCapacity> masks{};
    const auto append=[&](const layouts::RosterGroup& row,std::uint64_t mask,bool top) noexcept {
        if(!row.slotCount || row.slotCount>row.slotTypes.size())return false;
        for(std::size_t i=0;i<next.groupCount;++i)if(next.groups[i].key==row.registryKey){masks[i]|=mask;return true;}
        if(next.groupCount==next.groups.size())return false;
        const auto i=next.groupCount++;
        next.groups[i]={row.registryKey,std::span(row.slotTypes).first(row.slotCount),
            std::span(row.slotFlags).first(row.slotCount),std::span(row.slotIndices).first(row.slotCount)};
        masks[i]=mask;if(top)++next.topLevelGroupCount;
        if(top && !next.playerKeyGroup)
            for(std::size_t s=0;s<row.slotCount;++s)if(row.slotTypes[s]==13){next.playerKeyGroup=row.registryKey;break;}
        return true;
    };
    const auto owned=[](std::uint32_t key) noexcept {for(const auto& row:kRegistries)if(row.key==key)return true;return false;};
    for(std::size_t i=0;i<layout.rosterGroupCount;++i) {
        const auto index=layout.rosterGroups[i];if(index>=cache.size())return false;
        if(!creator && owned(cache[index].registryKey))continue;
        if(!append(cache[index],0,true))return false;
    }
    for(std::size_t i=0;i<layout.bubbleGroupCount;++i) {
        const auto index=layout.bubbleGroups[i];if(index>=cache.size())return false;
        if(!creator && owned(cache[index].registryKey))continue;
        if(!append(cache[index],layout.bubbleGroupMasks[i],false))return false;
    }
    if(creator)for(std::size_t i=0;i<kRegistries.size();++i) {
        const auto& authored=kRegistries[i];
        if(authored.bubble>=layout.bubbleCount || layout.bubbleHashes[authored.bubble]!=authored.bubbleHash
            || authored.slots.empty() || authored.slots.size()>layouts::kRosterSlotCapacity)return false;
        auto& row=storage.authored[i];row={};row.registryKey=authored.key;row.objectTag=authored.objectTag;
        row.slotCount=static_cast<std::uint16_t>(authored.slots.size());
        for(std::size_t s=0;s<authored.slots.size();++s) {
            row.slotTypes[s]=authored.slots[s].type;row.slotFlags[s]=authored.slots[s].flags();row.slotIndices[s]=authored.slots[s].index;
        }
        if(!append(row,std::uint64_t{1}<<authored.bubble,false))return false;
    }
    if(!next.playerKeyGroup)return false;
    std::size_t count{};
    for(std::uint32_t bubble=0;bubble<64;++bubble) {
        std::size_t keys{};
        for(std::size_t i=next.topLevelGroupCount;i<next.groupCount;++i)if(masks[i]&(std::uint64_t{1}<<bubble)) {
            if(keys==storage.keys[count].size())return false;
            storage.keys[count][keys++]=next.groups[i].key;
        }
        if(keys)storage.blocks[count++]={bubble,std::span(storage.keys[count]).first(keys)};
    }
    next.bubbleSubBlocks=std::span(storage.blocks).first(count);output=next;return true;
}
}
}
