#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
namespace dawn::state::activity::coo::native_generator {

inline constexpr std::uint8_t kSlotType=37;
inline constexpr std::uint32_t kComponentClass=0x80804EF6U,kSenseSchema=0x80805006U,kAuthSchema=0x80805007U;

inline constexpr std::size_t kRecordCount=2,kAnchorCount=4,kValueCount=5,kTopologyCount=2;
inline constexpr std::size_t kRegionCount=32,kGroupCount=64,kTileCapacity=100;

inline constexpr std::uint8_t kSeedWidth=32,kModeWidth=8,kCoordinateWidth=8,kRealWidth=32,kFlagWidth=1,
    kOverrideWidth=7,kValueWidth=32,kTileCountWidth=7,kTileWidth=16,kByteWidth=8;
// signed schema fields store zero at the middle of their unsigned wire range
inline constexpr std::uint32_t kCoordinateBias=128U,kModeBias=128U,kValueBias=0x80000000U,kTileBias=0x8000U;
inline constexpr std::size_t kTileBits=3U*kTileWidth;
inline constexpr std::size_t kRecordBits=kSeedWidth+kModeWidth
    +kAnchorCount*(2U*kCoordinateWidth+kRealWidth+kFlagWidth)
    +kOverrideWidth+kFlagWidth+kTopologyCount*kRealWidth+kValueCount*kValueWidth+kTileCountWidth;
inline constexpr std::size_t kTailBits=kSeedWidth+(kRegionCount+kGroupCount)*kByteWidth;
inline constexpr std::size_t kActivationBits=kRecordCount*kRecordBits+kTailBits;
inline constexpr std::size_t kActivationBytes=(kActivationBits+7U)/8U;
// SDK export: auth_min_bits 1750 is the no-tile body, auth_max_bits 11350 has both tile arrays full
inline constexpr std::size_t kMinimumBits=1750,kMaximumBits=11350;
static_assert(kRecordBits==475 && kTailBits==800 && kActivationBits==kMinimumBits);
static_assert(kActivationBits+kRecordCount*kTileCapacity*kTileBits==kMaximumBits);
static_assert(kActivationBytes==219);

inline constexpr std::uint8_t kOverrideSeedInput=0x01U;
inline constexpr std::uint8_t kOverrideAnchors=0x04U;
inline constexpr std::uint8_t kOverrideEnabled=0x08U;
inline constexpr std::uint8_t kOverrideMaximum=0x7FU;

// -1 means keep the authored default for the five i32 record values; they have no override bit
inline constexpr std::int32_t kKeepAuthoredValue=-1;
inline constexpr float kAuthoredTopology=-1.0F;
inline constexpr std::array<float,kTopologyCount> kAuthoredTopologies{kAuthoredTopology,kAuthoredTopology};

[[nodiscard]] constexpr bool finite(float value) noexcept {
    return (std::bit_cast<std::uint32_t>(value)&0x7F800000U)!=0x7F800000U;
}

struct Anchor final {
    std::int8_t column{-1};
    std::int8_t height{-1};
    float progress{};
    bool enabled{};
};

inline constexpr std::array<Anchor,kAnchorCount> kAuthoredAnchors{};
inline constexpr std::array<std::int32_t,kValueCount> kAuthoredValues{
    kKeepAuthoredValue,kKeepAuthoredValue,kKeepAuthoredValue,kKeepAuthoredValue,kKeepAuthoredValue};

struct Request final {
    bool enabled{true};
    std::uint32_t seed{};
    bool selectSeed{true};
    // values[0] is the solver's encounter placement budget; authored as zero, so a host
    // that leaves it at -1 places no encounter
    std::array<std::int32_t,kValueCount> values{kAuthoredValues};
    std::array<float,kTopologyCount> topology{kAuthoredTopologies};
    std::array<Anchor,kAnchorCount> anchors{kAuthoredAnchors};
    bool selectAnchors{};
    std::array<std::uint8_t,kRegionCount> regions{};
    std::array<std::uint8_t,kGroupCount> groups{};

    [[nodiscard]] constexpr std::uint8_t overrides() const noexcept {
        return static_cast<std::uint8_t>(kOverrideEnabled|(selectSeed?kOverrideSeedInput:0U)
            |(selectAnchors?kOverrideAnchors:0U));
    }
};

template<class Writer>
[[nodiscard]] bool write_record(Writer& writer,std::uint32_t seedInput,
                                const std::array<Anchor,kAnchorCount>& anchors,
                                const std::array<std::int32_t,kValueCount>& values,
                                std::uint8_t overrides,bool enabled,
                                const std::array<float,kTopologyCount>& topology=kAuthoredTopologies) noexcept {
    if(overrides>kOverrideMaximum) { return false; }
    for(const auto value:topology) { if(!finite(value)) { return false; } }
    const auto begin=writer.bit_count();
    if(!writer.write(seedInput,kSeedWidth) || !writer.write(kModeBias,kModeWidth)) { return false; }
    for(const auto& anchor:anchors) {
        if(!finite(anchor.progress)
            || !writer.write((static_cast<std::uint32_t>(anchor.column)+kCoordinateBias)&0xFFU,kCoordinateWidth)
            || !writer.write((static_cast<std::uint32_t>(anchor.height)+kCoordinateBias)&0xFFU,kCoordinateWidth)
            || !writer.write(std::bit_cast<std::uint32_t>(anchor.progress),kRealWidth)
            || !writer.write(anchor.enabled?1U:0U,kFlagWidth)) { return false; }
    }
    if(!writer.write(overrides,kOverrideWidth) || !writer.write(enabled?1U:0U,kFlagWidth)) { return false; }
    for(std::size_t index=0;index<kTopologyCount;++index) {
        if(!writer.write(std::bit_cast<std::uint32_t>(topology[index]),kRealWidth)) { return false; }
    }
    for(const auto value:values) {
        if(!writer.write(std::bit_cast<std::uint32_t>(value)+kValueBias,kValueWidth)) { return false; }
    }
    return writer.write(0U,kTileCountWidth) && writer.bit_count()-begin==kRecordBits;
}

template<class Writer>
[[nodiscard]] bool write_activation(Writer& writer,const Request& request) noexcept {
    const auto begin=writer.bit_count();
    if(!write_record(writer,request.selectSeed?request.seed:0U,
                     request.selectAnchors?request.anchors:kAuthoredAnchors,request.values,
                     request.overrides(),request.enabled,request.topology)) { return false; }
    // the second record is transition-scoped and no proven host path touches it, so it goes out
    // with a clear mask and every input at its authored-absent value
    if(!write_record(writer,0U,kAuthoredAnchors,kAuthoredValues,0U,false)) { return false; }
    if(!writer.write(request.seed,kSeedWidth)) { return false; }
    for(const auto region:request.regions) { if(!writer.write(region,kByteWidth)) { return false; } }
    for(const auto group:request.groups) { if(!writer.write(group,kByteWidth)) { return false; } }
    return writer.bit_count()-begin==kActivationBits;
}

inline constexpr std::uint32_t kForestGeneratorRegistry=0x2763EC91U;
inline constexpr std::uint16_t kForestGeneratorSlot=30;

// one masked host seed per run, kept across reattachment and region streaming, never zero
[[nodiscard]] constexpr std::uint32_t mission_seed(std::uint32_t entropy) noexcept {
    const std::uint32_t seed=entropy&0x7FFFFFFFU;
    return seed==0U?1U:seed;
}

[[nodiscard]] constexpr Request forest_request(std::uint32_t seed) noexcept {
    Request request{};
    request.seed=seed;
    request.values[0]=6;
    request.anchors={Anchor{2,0,0.0F,true},Anchor{0,2,0.0F,true},Anchor{2,0,1.0F,true},Anchor{0,2,0.0F,true}};
    request.selectAnchors=true;
    return request;
}

} // namespace dawn::state::activity::coo::native_generator
