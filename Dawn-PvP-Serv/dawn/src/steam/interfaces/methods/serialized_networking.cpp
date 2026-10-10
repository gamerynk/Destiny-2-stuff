#include "../internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../runtime/social/realtime_client.h"
#include "../../runtime/social/rendezvous_sender.h"
#include "../../runtime/transport/steam_transport_bridge.h"

namespace dawn::steam::interfaces::methods {
namespace {

std::atomic<unsigned> g_certificateCalls{};
std::atomic<unsigned> g_networkConfigCalls{};
std::atomic<unsigned> g_cacheRelayTicketCalls{};
std::atomic<unsigned> g_relayTicketCountCalls{};
std::atomic<unsigned> g_getRelayTicketCalls{};
std::atomic<unsigned> g_postConnectionStateCalls{};

void report_serialized(core::log::Level level, const char* format, ...) noexcept {
    std::array<char, 512> line{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         level,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

} // namespace

void serialized_send_rendezvous([[maybe_unused]] void* self,
                                std::uint64_t remoteId,
                                DWORD sourceConnectionId,
                                const void* message,
                                DWORD messageSize) noexcept {
    namespace realtime = runtime::social::realtime_client;
    namespace wire = middleware::social::realtime;
    const wire::Lobby lobby = realtime::lobby_snapshot();
    const bool member = std::any_of(lobby.members.begin(),
                                    lobby.members.begin() + lobby.memberCount,
                                    [remoteId](const wire::Member& entry) {
                                        return entry.platformId == remoteId;
                                    });
    // the receiving DLL's private callback has a fixed 0x200-byte packet buffer, so reject
    // larger ones
    const bool sizeValid = messageSize <= kSerializedRendezvousPacketCapacity;
    const bool pointerValid = message != nullptr || messageSize == 0;
    // a joiner's handshake runs while in its own solo lobby, so the owner it reaches is not a
    // local member. Gating on that dropped every handshake (open_nat); the server authorizes
    if (!sizeValid || !pointerValid) {
        report_serialized(core::log::Level::warn,
                          "ev=serialized_networking stage=send_rendezvous result=rejected"
                          " remote=0x%016llX lobby=0x%016llX source_connection=%lu"
                          " bytes=%lu member=%u size_valid=%u pointer_valid=%u",
                          static_cast<unsigned long long>(remoteId),
                          static_cast<unsigned long long>(lobby.id),
                          static_cast<unsigned long>(sourceConnectionId),
                          static_cast<unsigned long>(messageSize),
                          member ? 1U : 0U,
                          sizeValid ? 1U : 0U,
                          pointerValid ? 1U : 0U);
        return;
    }
    wire::Request request{};
    request.operation = wire::Operation::sendRendezvous;
    request.lobbyId = lobby.id;
    request.targetPlatformId = remoteId;
    request.payloadSize = sizeof(sourceConnectionId) + messageSize;
    std::memcpy(request.payload.data(), &sourceConnectionId, sizeof(sourceConnectionId));
    if (messageSize != 0) {
        std::memcpy(request.payload.data() + sizeof(sourceConnectionId), message, messageSize);
    }
    // runs on the native networking thread; a synchronous backend round trip per packet
    // stalled the transport (stutter, then timeout kick). The background sender batches FIFO
    if (!runtime::social::rendezvous_sender::enqueue(request)) {
        report_serialized(core::log::Level::warn,
                          "ev=serialized_networking stage=send_rendezvous result=queue_full"
                          " remote=0x%016llX lobby=0x%016llX source_connection=%lu bytes=%lu",
                          static_cast<unsigned long long>(remoteId),
                          static_cast<unsigned long long>(lobby.id),
                          static_cast<unsigned long>(sourceConnectionId),
                          static_cast<unsigned long>(messageSize));
    }
}

void serialized_send_failure([[maybe_unused]] void* self,
                             std::uint64_t remoteId,
                             DWORD destinationConnectionId,
                             DWORD reason,
                             const char* reasonText) noexcept {
    report_serialized(core::log::Level::error,
                      "ev=serialized_networking stage=send_failure result=received"
                      " remote=0x%016llX destination_connection=%lu reason=%lu text=%.192s",
                      static_cast<unsigned long long>(remoteId),
                      static_cast<unsigned long>(destinationConnectionId),
                      static_cast<unsigned long>(reason),
                      reasonText != nullptr ? reasonText : "");
}

ApiCall serialized_get_certificate([[maybe_unused]] void* self) noexcept {
    const unsigned call = g_certificateCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    const ApiCall result = runtime::transport::request_certificate();
    if (call <= 16U) {
        report_serialized(result != 0 ? core::log::Level::info : core::log::Level::error,
                          "ev=serialized_networking stage=get_certificate result=%s call=%u",
                          result != 0 ? "requested" : "failed",
                          call);
    }
    return result;
}

int serialized_get_network_config([[maybe_unused]] void* self,
                                  void* output,
                                  DWORD capacity,
                                  const char* launcherPartner) noexcept {
    const int result = runtime::transport::network_config(output, capacity, launcherPartner);
    const unsigned call = g_networkConfigCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (call <= 16U) {
        report_serialized(core::log::Level::info,
                          "ev=serialized_networking stage=get_network_config result=forwarded"
                          " bytes=%d capacity=%lu partner=%.96s call=%u",
                          result,
                          static_cast<unsigned long>(capacity),
                          launcherPartner != nullptr ? launcherPartner : "",
                          call);
    }
    return result;
}

void serialized_cache_relay_ticket([[maybe_unused]] void* self,
                                   const void* ticket,
                                   DWORD ticketSize) noexcept {
    runtime::transport::cache_relay_ticket(ticket, ticketSize);
    const unsigned call = g_cacheRelayTicketCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (call <= 16U) {
        report_serialized(core::log::Level::warn,
                          "ev=serialized_networking stage=cache_relay_ticket result=forwarded"
                          " bytes=%lu call=%u",
                          static_cast<unsigned long>(ticketSize),
                          call);
    }
}

DWORD serialized_relay_ticket_count([[maybe_unused]] void* self) noexcept {
    const unsigned call = g_relayTicketCountCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    const DWORD result = runtime::transport::relay_ticket_count();
    if (call <= 16U) {
        report_serialized(core::log::Level::info,
                          "ev=serialized_networking stage=relay_ticket_count result=forwarded"
                          " tickets=%lu call=%u",
                          static_cast<unsigned long>(result),
                          call);
    }
    return result;
}

int serialized_get_relay_ticket([[maybe_unused]] void* self,
                                DWORD index,
                                void* output,
                                DWORD capacity) noexcept {
    const unsigned call = g_getRelayTicketCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    const int result = runtime::transport::relay_ticket(index, output, capacity);
    if (call <= 16U) {
        report_serialized(core::log::Level::info,
                          "ev=serialized_networking stage=get_relay_ticket result=forwarded"
                          " bytes=%d index=%lu capacity=%lu call=%u",
                          result,
                          static_cast<unsigned long>(index),
                          static_cast<unsigned long>(capacity),
                          call);
    }
    return result;
}

void serialized_post_connection_state([[maybe_unused]] void* self,
                                      const void* message,
                                      DWORD messageSize) noexcept {
    runtime::transport::post_connection_state(message, messageSize);
    const unsigned call = g_postConnectionStateCalls.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (call <= 32U) {
        report_serialized(core::log::Level::info,
                          "ev=serialized_networking stage=post_connection_state result=forwarded"
                          " bytes=%lu call=%u",
                          static_cast<unsigned long>(messageSize),
                          call);
    }
}

} // namespace dawn::steam::interfaces::methods
