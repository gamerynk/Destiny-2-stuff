#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace dawn::middleware::mission::observation_wire {

// client half of a server-run mission: facts that exist only in client memory (enemy source,
// death, health/AI binding, actor pool fill, dialogue dispatch), batched to a dedicated server
inline constexpr char kPath[] = "/v1/mission/observe";
inline constexpr std::uint32_t kRequestMagic = 0x314F4D44;
inline constexpr std::uint32_t kResponseMagic = 0x31524D44;
inline constexpr std::size_t kReceiptCapacity = 96;
inline constexpr std::size_t kReadinessCapacity = 12;

enum class Kind : std::uint8_t {
    admission = 1,
    death = 2,
    readiness = 3,
    capacity = 4,
    submission = 5,
    health = 6,
};

struct Enemy final {
    std::uint64_t run{};
    std::uint32_t actor{UINT32_MAX};
    std::uint32_t owner{UINT32_MAX};
    std::uint32_t generation{};
    std::uint16_t source{};
    std::uint32_t registry{};
    friend bool operator==(const Enemy&, const Enemy&) = default;
};

struct Readiness final {
    bool created{}, health{}, ai{}, tactical{};
    std::uint32_t healthHandle{UINT32_MAX};
    std::uint32_t tacticalRegistry{};
    std::uint16_t tacticalSlot{};
    std::int8_t tacticalRow{-1};
};

struct Capacity final {
    std::uint64_t run{};
    bool known{};
    std::uint32_t used{}, maximum{};
};

struct Submission final {
    std::uint64_t run{};
    std::uint32_t definition{};
    std::int64_t offset{};
    std::uint32_t bank{};
    std::uint8_t row{};
    std::uint32_t generation{};
};

struct Receipt final {
    Kind kind{};
    Enemy enemy{};
    Readiness readiness{};
    Capacity capacity{};
    Submission submission{};
    float healthFraction{};
};

struct Request final {
    std::uint64_t run{};
    std::array<Receipt, kReceiptCapacity> receipts{};
    std::size_t count{};
};

struct Boss final {
    std::uint64_t ownerRun{};
    std::uint32_t ownerValue{};
    Enemy enemy{};
    std::uint8_t stage{};
    std::uint32_t revision{};
    bool requested{};
};

struct Response final {
    std::uint64_t run{};
    Boss boss{};
    std::array<Enemy, kReadinessCapacity> readiness{};
    std::size_t readinessCount{};
};

namespace detail {

class Writer final {
public:
    explicit Writer(std::span<std::byte> out) noexcept : out_(out) {}
    template <class T> bool put(T value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (sizeof(T) > out_.size() - used_) {
            return false;
        }
        std::memcpy(out_.data() + used_, &value, sizeof(T));
        used_ += sizeof(T);
        return true;
    }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }

private:
    std::span<std::byte> out_;
    std::size_t used_{};
};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> in) noexcept : in_(in) {}
    template <class T> bool get(T& value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (sizeof(T) > in_.size() - used_) {
            return false;
        }
        std::memcpy(&value, in_.data() + used_, sizeof(T));
        used_ += sizeof(T);
        return true;
    }
    [[nodiscard]] bool done() const noexcept { return used_ == in_.size(); }

private:
    std::span<const std::byte> in_;
    std::size_t used_{};
};

inline bool put(Writer& w, const Enemy& e) noexcept {
    return w.put(e.run) && w.put(e.actor) && w.put(e.owner) && w.put(e.generation)
           && w.put(e.source) && w.put(e.registry);
}
inline bool get(Reader& r, Enemy& e) noexcept {
    return r.get(e.run) && r.get(e.actor) && r.get(e.owner) && r.get(e.generation)
           && r.get(e.source) && r.get(e.registry);
}

} // namespace detail

static_assert(std::endian::native == std::endian::little, "the wire is little endian");

[[nodiscard]] inline bool encode(const Request& request, std::span<std::byte> out,
                                 std::size_t& written) noexcept {
    written = 0;
    if (request.count > kReceiptCapacity) {
        return false;
    }
    detail::Writer w(out);
    bool ok = w.put(kRequestMagic) && w.put(request.run)
              && w.put(static_cast<std::uint8_t>(request.count));
    for (std::size_t i = 0; ok && i < request.count; ++i) {
        const Receipt& r = request.receipts[i];
        ok = w.put(static_cast<std::uint8_t>(r.kind));
        switch (r.kind) {
        case Kind::admission:
        case Kind::death:
            ok = ok && detail::put(w, r.enemy);
            break;
        case Kind::readiness: {
            const std::uint8_t flags = static_cast<std::uint8_t>(
                (r.readiness.created ? 1U : 0U) | (r.readiness.health ? 2U : 0U)
                | (r.readiness.ai ? 4U : 0U) | (r.readiness.tactical ? 8U : 0U));
            ok = ok && detail::put(w, r.enemy) && w.put(flags) && w.put(r.readiness.healthHandle)
                 && w.put(r.readiness.tacticalRegistry) && w.put(r.readiness.tacticalSlot)
                 && w.put(r.readiness.tacticalRow);
            break;
        }
        case Kind::capacity:
            ok = ok && w.put(r.capacity.run) && w.put(static_cast<std::uint8_t>(r.capacity.known))
                 && w.put(r.capacity.used) && w.put(r.capacity.maximum);
            break;
        case Kind::submission:
            ok = ok && w.put(r.submission.run) && w.put(r.submission.definition)
                 && w.put(r.submission.offset) && w.put(r.submission.bank) && w.put(r.submission.row)
                 && w.put(r.submission.generation);
            break;
        case Kind::health:
            ok = ok && detail::put(w, r.enemy) && w.put(r.healthFraction);
            break;
        default:
            ok = false;
        }
    }
    if (ok) {
        written = w.used();
    }
    return ok;
}

[[nodiscard]] inline bool decode(std::span<const std::byte> in, Request& request) noexcept {
    request = {};
    detail::Reader r(in);
    std::uint32_t magic{};
    std::uint8_t count{};
    if (!r.get(magic) || magic != kRequestMagic || !r.get(request.run) || !r.get(count)
        || count > kReceiptCapacity) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        Receipt& out = request.receipts[i];
        std::uint8_t kind{};
        if (!r.get(kind)) {
            return false;
        }
        out.kind = static_cast<Kind>(kind);
        bool ok = false;
        switch (out.kind) {
        case Kind::admission:
        case Kind::death:
            ok = detail::get(r, out.enemy);
            break;
        case Kind::readiness: {
            std::uint8_t flags{};
            ok = detail::get(r, out.enemy) && r.get(flags) && r.get(out.readiness.healthHandle)
                 && r.get(out.readiness.tacticalRegistry) && r.get(out.readiness.tacticalSlot)
                 && r.get(out.readiness.tacticalRow);
            out.readiness.created = (flags & 1U) != 0;
            out.readiness.health = (flags & 2U) != 0;
            out.readiness.ai = (flags & 4U) != 0;
            out.readiness.tactical = (flags & 8U) != 0;
            break;
        }
        case Kind::capacity: {
            std::uint8_t known{};
            ok = r.get(out.capacity.run) && r.get(known) && r.get(out.capacity.used)
                 && r.get(out.capacity.maximum);
            out.capacity.known = known != 0;
            break;
        }
        case Kind::submission:
            ok = r.get(out.submission.run) && r.get(out.submission.definition)
                 && r.get(out.submission.offset) && r.get(out.submission.bank)
                 && r.get(out.submission.row) && r.get(out.submission.generation);
            break;
        case Kind::health:
            ok = detail::get(r, out.enemy) && r.get(out.healthFraction);
            break;
        default:
            return false;
        }
        if (!ok) {
            return false;
        }
    }
    request.count = count;
    return r.done();
}

[[nodiscard]] inline bool encode(const Response& response, std::span<std::byte> out,
                                 std::size_t& written) noexcept {
    written = 0;
    if (response.readinessCount > kReadinessCapacity) {
        return false;
    }
    detail::Writer w(out);
    bool ok = w.put(kResponseMagic) && w.put(response.run) && w.put(response.boss.ownerRun)
              && w.put(response.boss.ownerValue) && detail::put(w, response.boss.enemy)
              && w.put(response.boss.stage) && w.put(response.boss.revision)
              && w.put(static_cast<std::uint8_t>(response.boss.requested))
              && w.put(static_cast<std::uint8_t>(response.readinessCount));
    for (std::size_t i = 0; ok && i < response.readinessCount; ++i) {
        ok = detail::put(w, response.readiness[i]);
    }
    if (ok) {
        written = w.used();
    }
    return ok;
}

[[nodiscard]] inline bool decode(std::span<const std::byte> in, Response& response) noexcept {
    response = {};
    detail::Reader r(in);
    std::uint32_t magic{};
    std::uint8_t requested{}, count{};
    if (!r.get(magic) || magic != kResponseMagic || !r.get(response.run)
        || !r.get(response.boss.ownerRun) || !r.get(response.boss.ownerValue)
        || !detail::get(r, response.boss.enemy) || !r.get(response.boss.stage)
        || !r.get(response.boss.revision) || !r.get(requested) || !r.get(count)
        || count > kReadinessCapacity) {
        return false;
    }
    response.boss.requested = requested != 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!detail::get(r, response.readiness[i])) {
            return false;
        }
    }
    response.readinessCount = count;
    return r.done();
}

} // namespace dawn::middleware::mission::observation_wire
