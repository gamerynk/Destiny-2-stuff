#pragma once
#include "native_cursor_simple_tail.h"

namespace dawn::client::hooks::cursor::pending_tail {

using State = simple_tail::State;
using Owner = simple_tail::Owner;
using Lease = graphics::input::RawOpeningLease;
using LeaseSnapshot = graphics::input::RawOpeningSnapshot;

struct Bindings {
    void* context{};
    bool (*readState)(void*, State&) noexcept{};
    bool (*readGuardedState)(void*, State&) noexcept{};
    bool (*claimRaw)(void*, std::uint64_t, std::uint64_t, Lease&) noexcept{};
    LeaseSnapshot (*rawSnapshot)(void*, const Lease&) noexcept{};
    bool (*releaseRaw)(void*, const Lease&) noexcept{};
    graphics::input::RawInputSnapshot (*rawBinding)(void*) noexcept{};
    bool (*clearMouse)(void*) noexcept{};
    std::uint64_t (*commitOpen)(void*, UINT key, std::uint64_t hiddenGeneration) noexcept{};
    bool (*commitClose)(void*, UINT key, std::uint64_t visibleGeneration) noexcept{};
    void (*applyInputVisibility)(void*, bool) noexcept{};
    bool (*currentClientRectangle)(void*, HWND, RECT&) noexcept{};
    bool (*clip)(void*, const RECT*) noexcept{};
    void (*requestGuard)(void*) noexcept{};
    void (*prepareGuardedVisibility)(void*, bool) noexcept{};
    bool (*guardAttached)(void*) noexcept{};
};
enum class Result {
    disabled, busy, idle, requested, waitingOwner, waitingDrain, opened, released,
    closed, cancelled, stale, clearFailed, commitFailed, geometryFailed,
    clipFailed, rawReleaseFailed
};

class Policy final {
public:
    [[nodiscard]] bool configure(bool optIn, const Bindings&) noexcept;
    [[nodiscard]] Result request_toggle(UINT key) noexcept;
    [[nodiscard]] Result observe_owner(const Owner&) noexcept;
    [[nodiscard]] bool suspend_window(const Owner&) noexcept;
    [[nodiscard]] bool handoff_guarded(const Owner&, bool intendedVisible) noexcept;
    [[nodiscard]] bool intent_open() const noexcept;
    void stop() noexcept;
    [[nodiscard]] bool shutdown(const Owner&) noexcept;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool owns_capture() const noexcept;
    [[nodiscard]] bool pending() const noexcept;
private:
    struct Operation { std::atomic_flag& flag; ~Operation() { flag.clear(std::memory_order_release); } };
    [[nodiscard]] bool read(State&) const noexcept;
    [[nodiscard]] bool owned(const Owner&, const State&) const noexcept;
    [[nodiscard]] bool same_window(const State&, const State&) const noexcept;
    [[nodiscard]] bool unchanged(const Owner&, const State&, bool visible) const noexcept;
    [[nodiscard]] Result fallback(Result) noexcept;
    [[nodiscard]] Result close(const Owner&, const State&) noexcept;
    [[nodiscard]] Result cancel(const Owner&, const State&) noexcept;
    Bindings bindings_{};
    State admitted_{};
    Lease lease_{};
    UINT key_{};
    std::uint64_t committedGeneration_{};
    bool cleared_{};
    bool closeRequested_{};
    std::atomic_flag operation_ = ATOMIC_FLAG_INIT;
    std::atomic_bool configured_{}, accepting_{}, captureOwned_{}, pending_{};
    std::atomic_bool intentOpen_{};
};
}
