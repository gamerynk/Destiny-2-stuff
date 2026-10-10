#pragma once
#include "activity_notification_frame.h"
#include "../../../../../state/activity/entity_retirement/runtime.h"
namespace dawn::server::bap::encrypted::push::activity {
bool append_retirement(Scratch& scratch,std::uint64_t activity,
                        const state::activity::entity_retirement::History::Operation& op,
                        std::span<const std::byte,state::kAesKeySize> key,
                        std::array<std::byte,state::kBapNonceSize>& nonce,
                        std::span<std::byte> output,std::size_t& written) noexcept;
bool consume_retirement(Session& session,Scratch& scratch,std::span<std::byte> response,
                         std::size_t& written,bool& touchesScratch) noexcept;
bool consume_departure_query(Session& session,Scratch& scratch,std::span<std::byte> response,
                              std::size_t& written,bool& touchesScratch) noexcept;
}
