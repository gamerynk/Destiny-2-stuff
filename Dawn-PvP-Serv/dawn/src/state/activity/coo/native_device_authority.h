#pragma once
#include "../../../middleware/bap/activity_message/native/generic_device_authority.h"
#include <bit>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include "../../../middleware/bap/activity_message/native/capture_controller_authority.h"
#include "../../../middleware/bap/activity_message/native/public_event_interaction_authority.h"
namespace dawn::state::activity::coo::native_device {
namespace generic=middleware::bap::activity_message::native::generic_device;
namespace interaction=middleware::bap::activity_message::native::interaction;
[[nodiscard]] inline bool inactive_state(std::span<const std::byte,0x44> state) noexcept {
    const auto read32=[](const std::byte* b) noexcept { std::uint32_t v{};std::memcpy(&v,b,sizeof v);return v; };
    const auto read64=[](const std::byte* b) noexcept { std::uint64_t v{};std::memcpy(&v,b,sizeof v);return v; };
    const auto generation = read32(state.data());
    if (generation == 0U || generation >= 0x7FFFFFFFU
        || read32(state.data() + 4) != 0U
        || state[8] != std::byte{} || state[9] != std::byte{}
        || read32(state.data() + 0xC) != 0xFFFFFFFFU
        || read64(state.data() + 0x10) != 0xFFFF00FF811C9DC5ULL
        || read32(state.data() + 0x20) != 0U
        || read32(state.data() + 0x24) != 0U
        || read32(state.data() + 0x28) != 0U
        || read32(state.data() + 0x2C) != 0x3F800000U
        || state[0x30] != std::byte{}
        || read32(state.data() + 0x40) != 0U) { return false; }
    return true;
}
template<class Writer> bool collection(Writer& w,std::uint32_t registry,std::span<const std::uint16_t> sources) noexcept {
    if(sources.size()>8 || !w.write(static_cast<std::uint32_t>(sources.size()),4)) { return false; }
    for(const auto source:sources) {
        if(!w.write(1,1) || !w.write(0x80809579U,32) || !w.write(1,2)
            || !w.write(registry,32) || !w.write(2,7) || !w.write(32768U+source,16)) { return false; }
    }
    return true;
}
template<class Writer> bool collection_sources(Writer& w,std::uint32_t registry,std::span<const std::uint16_t> sources) noexcept {
    if(sources.size()>8 || !w.write(static_cast<std::uint32_t>(sources.size()),4)) { return false; }
    for(const auto source:sources) {
        if(!w.write(1,1) || !w.write(0x80809578U,32) || !w.write(1,2)
            || !w.write(registry,32) || !w.write(2,7) || !w.write(32768U+source,16)) { return false; }
    }
    return true;
}
template<class Writer> bool linked_effect(Writer& w,std::uint32_t registry,std::uint16_t collection,bool enabled) noexcept {
    if(!w.write(0,1) || !w.write(enabled?0U:1U,1)) { return false; }
    for(unsigned i=0;i<4;++i) { if(!w.write(0x80000000U,32)) { return false; } }
    return w.write(registry,32) && w.write(35,7) && w.write(32768U+collection,16) && w.write(0,1);
}
// inactive preparation has no entity to take component state; keep the canonical empty dynamic
// list or inactive_state rejects it and the active generation is never published
[[nodiscard]] inline constexpr std::size_t object_bits(bool active,bool capture=false,
    interaction::Mode mode=interaction::Mode::unchanged,bool pose=false) noexcept {
    if(!interaction::valid(mode)) {return 0;}
    return 252U+(active && capture?middleware::bap::activity_message::native::capture_controller::kRecordBits:0U)
        +(active && mode!=interaction::Mode::unchanged?1U+interaction::kPayloadBits:0U)
        +(active && pose?generic::kRecordBits:0U);
}
template<class Writer> bool object(Writer& writer,std::uint32_t generation,bool active,
    const middleware::bap::activity_message::native::capture_controller::State* capture=nullptr,
    interaction::Mode mode=interaction::Mode::unchanged,const generic::State* pose=nullptr) noexcept {
    if(!interaction::valid(mode)) {return false;}
    if(!active) {capture=nullptr;mode=interaction::Mode::unchanged;pose=nullptr;}
    const bool interacting=mode!=interaction::Mode::unchanged;
    return writer.write(generation ^ 0x80000000U, 32)
        && writer.write(0x80000000U, 32)
        && writer.write(active ? 1U : 0U, 1)
        && writer.write(0U, 1)
        && writer.write(0x7FFFFFFFU, 32)
        && writer.write(0x811C9DC5U, 32)
        && writer.write(0U, 7)
        && writer.write(0x7FFFU, 16)
        && writer.write(0U, 32) && writer.write(0U, 32) && writer.write(0U, 32)
        && writer.write(0U, 1)
        && writer.write((capture?1U:0U)+(interacting?1U:0U)+(pose?1U:0U), 2)
        && (!capture || middleware::bap::activity_message::native::capture_controller::write_record(writer,*capture))
        && (!interacting || interaction::write_record(writer,mode))
        && (!pose || generic::write_record(writer,*pose));
}

// absent revisions (-1) preserve native power and lock defaults
template<class Writer> bool position_only(Writer& writer,float position,std::int16_t revision,bool snap) noexcept {
    return writer.write(std::bit_cast<std::uint32_t>(position),32)
        && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(revision)+0x8000),16)
        && writer.write(snap?1U:0U,1)
        && writer.write(0x3F800000U,32) && writer.write(0x7FFFU,16) && writer.write(0,1)
        && writer.write(0,32) && writer.write(0x7FFFU,16) && writer.write(0,1);
}

template<class Writer> bool channels(Writer& writer,float position,std::uint16_t revision) noexcept {
    const auto channel=[&](float value,std::uint16_t rev) noexcept {
        return writer.write(std::bit_cast<std::uint32_t>(value),32)
            && writer.write(static_cast<std::uint32_t>(rev)+0x8000U,16)
            && writer.write(0U,1);
    };
    return channel(position,revision) && channel(1.F,0) && channel(0.F,0);
}

}
