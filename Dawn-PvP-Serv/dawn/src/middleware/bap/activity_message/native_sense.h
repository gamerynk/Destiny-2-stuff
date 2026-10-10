#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include "scene_sense.h"
#include "squad_sense.h"
#include "monitor_sense.h"
#include "combatant_sense.h"
#include "object_sense.h"
#include "native/forest_generator_sense.h"
#include "native/public_event_engagement_sense.h"

namespace dawn::middleware::bap::activity_message::native_sense {
struct SourceDelta {
    std::array<std::uint32_t,6> scalar{};
    std::array<std::int32_t,8> consumed{};
    std::uint8_t present{},consumedCount{};
    bool consumedPresent{};
};
// pinned executable reflection; raw codes, never inferred actor deaths
struct Output {
    std::uint32_t schema{};
    std::uint32_t revision{};
    bool root{};
    scene_sense::Output scene{};
    squad_sense::Output squad{};
    monitor_sense::Output monitor{};
    combatant_sense::Output combatant{};
    object_sense::Output object{};
    std::uint32_t generatorSeed{},generatorRegions{};
    std::uint64_t generatorGroups{};
    SourceDelta source{};
    native::forest_generator_sense::Progress generator{};
    native::engagement_sense::Output engagement{};
};
[[nodiscard]] constexpr std::uint32_t schema(std::uint8_t type) noexcept {
    switch (type) {
    case 1: return 0x80807ECC;
    case 2: return 0x80807DA2;
    case 4: return 0x8080992E;
    case 23: return 0x80804F47;
    case 30: return 0x80809531;
    case 37: return native::forest_generator_sense::kSchema;
    case 43: return 0x8080626A;
    case 70: return 0x808094F0;
    default: return 0;
    }
}
template<class Reader> bool optional(Reader& reader, std::size_t width) noexcept {
    std::uint64_t present{};
    return reader.read(1,present) && (!present || reader.skip(width));
}
template<class Reader> bool source(Reader& reader, SourceDelta& output) noexcept {
    std::uint64_t present{},count{};
    SourceDelta result{};
    constexpr std::array<std::uint8_t,6> widths{31,31,31,6,7,31};
    for(unsigned i=0;i<widths.size();++i) {
        if(!reader.read(1,present)) return false;
        if(present) {
            std::uint64_t value{};
            if(!reader.read(widths[i],value)) return false;
            result.present|=static_cast<std::uint8_t>(1U<<i);
            result.scalar[i]=static_cast<std::uint32_t>(value);
        }
    }
    if(!reader.skip(8)) return false;
    if (!reader.read(1,present)) return false;
    if(present) {
        if(!reader.read(4,count) || count>8) return false;
        result.consumedPresent=true;result.consumedCount=static_cast<std::uint8_t>(count);
        for(unsigned i=0;i<count;++i) {
            std::uint64_t value{};
            if(!reader.read(32,value)) return false;
            result.consumed[i]=static_cast<std::int32_t>(static_cast<std::int64_t>(value)-2147483648LL);
        }
    }
    if (!reader.read(1,present)) return false;
    if (present) for (unsigned i=0;i<24;++i) if (!optional(reader,7)) return false;
    output=result;return true;
}
template<class Reader> bool member(Reader& reader) noexcept {
    if (!optional(reader,31) || !optional(reader,9) || !optional(reader,31)) return false;
    std::uint64_t present{},nested{};
    if (!reader.read(1,present)) return false;
    if (present && (!optional(reader,6) || !optional(reader,31)
                    || !optional(reader,31) || !reader.skip(1))) return false;
    if (!reader.read(1,present)) return false;
    if (present) {
        if (!reader.read(1,nested)) return false;
        if (nested) for (unsigned i=0;i<8;++i) if (!optional(reader,31)) return false;
        if (!optional(reader,32)) return false;
    }
    return optional(reader,31) && reader.skip(2) && optional(reader,31)
        && optional(reader,7) && optional(reader,7) && reader.skip(2);
}
template<class Reader>
[[nodiscard]] bool read(Reader& reader, std::uint8_t type, Output& output,
                        std::size_t& width) noexcept {
    Output result{};
    result.schema=schema(type);
    if (!result.schema) return false;
    if(type==70) {
        if(!native::engagement_sense::read(reader,result.engagement,width))return false;
        result.root=result.engagement.root;result.revision=result.engagement.revision;output=result;return true;
    }
    if(type==37) {
        native::forest_generator_sense::Output generator{};
        if(!native::forest_generator_sense::read(reader,generator,width))return false;
        result.root=generator.root;result.revision=generator.revision;result.generator=generator.progress;
        result.generatorSeed=generator.progress.reportedSeed;
        result.generatorRegions=generator.progress.clearedAreas;
        result.generatorGroups=generator.progress.openedGroups;
        output=result;return true;
    }
    if (type==43) {
        if (!scene_sense::read(reader,result.scene,width)) return false;
        result.root=result.scene.delta; result.revision=result.scene.revision;
        output=result; return true;
    }
    const auto before=reader.remaining_bits();
    std::uint64_t value{};
    if (!reader.read(1,value)) return false;
    result.root=value!=0;
    if (result.root) {
        if (type==1) {
            auto squadReader=reader;
            if (!squad_sense::read_delta(squadReader,result.squad)
                || !source(reader,result.source)
                || squadReader.remaining_bits()!=reader.remaining_bits()) return false;
        }
        if (type==2 && !combatant_sense::read_delta(reader,result.combatant)) return false;
        if (type==4 && !object_sense::read(reader,result.object)) return false;
        if (type==23) for (unsigned i=0;i<6;++i) if (!optional(reader,32)) return false;
        if (type==30 && !monitor_sense::read(reader,result.monitor)) return false;
    }
    if (!reader.read(32,value)) return false;
    result.revision=static_cast<std::uint32_t>(value);
    width=before-reader.remaining_bits(); output=result; return true;
}
} // namespace dawn::middleware::bap::activity_message::native_sense
