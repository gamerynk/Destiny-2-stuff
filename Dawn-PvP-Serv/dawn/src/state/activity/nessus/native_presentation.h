#pragma once
#include "failsafe_presentation.h"
namespace dawn::state::activity::nessus::native_presentation {
[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    return failsafe_presentation::owns(key,type,slot)?failsafe_presentation::body_bits(frame,key,type,slot):nessus::body_bits(frame,key,type,slot);
}
template<class Writer>[[nodiscard]] bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    return failsafe_presentation::owns(key,type,slot)?failsafe_presentation::write_body(writer,frame,key,type,slot):nessus::write_body(writer,frame,key,type,slot);
}
}
