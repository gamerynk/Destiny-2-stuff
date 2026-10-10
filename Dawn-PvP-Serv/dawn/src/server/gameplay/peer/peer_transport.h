#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../middleware/encoding/bit_writer.h"
#include "../../../state/gameplay/definition.h"

namespace dawn::server::gameplay::peer {

void deliver(const state::gameplay::Endpoint& from,
             std::span<const std::byte> payload,
             std::uint64_t now) noexcept;

[[nodiscard]] bool enqueue_reliable(std::uint64_t sessionId,
                                    std::uint8_t id,
                                    std::uint32_t declaredSize,
                                    std::span<const std::byte> body,
                                    std::size_t bodyBits) noexcept;

[[nodiscard]] bool enqueue_reliable_to(const state::gameplay::Endpoint& endpoint,
                                       std::uint64_t sessionId,
                                       std::uint8_t id,
                                       std::uint32_t declaredSize,
                                       std::span<const std::byte> body,
                                       std::size_t bodyBits) noexcept;

[[nodiscard]] bool
remote_address(std::uint64_t sessionId,
               std::array<std::byte, state::gameplay::kNetAddrBlobSize>& output) noexcept;

[[nodiscard]] bool remote_address_for(
    const state::gameplay::Endpoint& endpoint,
    std::uint64_t sessionId,
    std::array<std::byte, state::gameplay::kNetAddrBlobSize>& output) noexcept;

// group-session messages must wait for this or the peer acks their transport packet without
// dispatching the reliable records
[[nodiscard]] bool application_ready(std::uint64_t sessionId) noexcept;
[[nodiscard]] bool application_ready_for(const state::gameplay::Endpoint& endpoint,
                                         std::uint64_t sessionId) noexcept;

inline constexpr std::size_t kOutOfBandBodyCapacity = 128;

[[nodiscard]] bool send_container(const state::gameplay::Endpoint& to,
                                  std::uint8_t id,
                                  std::uint32_t declaredSize,
                                  std::span<const std::byte> body,
                                  std::size_t bodyBits) noexcept;

template <typename Body>
[[nodiscard]] bool send_out_of_band(const state::gameplay::Endpoint& to,
                                    std::uint8_t id,
                                    std::uint32_t declaredSize,
                                    Body write) noexcept {
    std::array<std::byte, kOutOfBandBodyCapacity> body{};
    middleware::encoding::bits::Writer writer(body);
    std::size_t size = 0;
    if (!write(writer) || !writer.finish(size)) {
        return false;
    }
    return send_container(to, id, declaredSize, {body.data(), size}, writer.bit_count());
}

void bind_view(std::uint64_t sessionId, const state::gameplay::ViewSignature& signature) noexcept;
void bind_view_for(const state::gameplay::Endpoint& endpoint,
                   std::uint64_t sessionId,
                   const state::gameplay::ViewSignature& signature) noexcept;

[[nodiscard]] bool view_bound(std::uint64_t sessionId) noexcept;
[[nodiscard]] bool view_bound_for(const state::gameplay::Endpoint& endpoint,
                                  std::uint64_t sessionId) noexcept;

[[nodiscard]] bool link_stage(std::uint64_t sessionId, state::gameplay::PeerStage& stage) noexcept;

void service(std::uint64_t now) noexcept;

void drop(std::uint64_t sessionId) noexcept;
void drop_for(const state::gameplay::Endpoint& endpoint, std::uint64_t sessionId) noexcept;

void drop_endpoint(const state::gameplay::Endpoint& endpoint) noexcept;

void reset() noexcept;

} // namespace dawn::server::gameplay::peer
