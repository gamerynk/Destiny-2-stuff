#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>

#include "../graphics/input/input.h"

namespace dawn::client::hooks::cursor::simple_tail {

struct State {
    std::uint64_t visibilityGeneration{};
    std::uint64_t mainBindingGeneration{};
    std::uint64_t windowRevision{};
    HWND window{};
    HWND nativeWindow{};
    DWORD windowThread{};
    unsigned marker{}; // native 2686998: RECT published is 1, NULL published is 0
    bool visible{};
    bool visibilityAvailable{};
    bool mainBindingOwned{};
    bool nativeContractQualified{};
    bool primaryPauseOwned{};
};

struct Owner {
    HWND window{};
    DWORD thread{};
    bool outsideNativeOwnership{};
};

struct Bindings {
    void* context{};
    bool (*readState)(void*, State&) noexcept{};
    bool (*guardAttached)(void*) noexcept{};
    void (*requestGuard)(void*) noexcept{};
    graphics::input::RawInputSnapshot (*rawSnapshot)(void*) noexcept{};
    bool (*claimRaw)(void*, std::uint64_t, std::uint64_t,
                     graphics::input::RawCaptureClaim&) noexcept{};
    bool (*releaseRaw)(void*, const graphics::input::RawCaptureClaim&) noexcept{};
    bool (*clearMouse)(void*) noexcept{};
    bool (*currentClientRectangle)(void*, HWND, RECT&) noexcept{};
    bool (*clip)(void*, const RECT*) noexcept{};
};

enum class Result {
    disabled, busy, openingAllowed, waitingGuard, waitingOwner, waitingRawDrain,
    opened, released, closed, stale, unknownNativeState, clearFailed,
    geometryFailed, clipFailed, rawReleaseFailed
};

class Policy final {
public:
    [[nodiscard]] bool configure(bool optIn, const Bindings&) noexcept;
    [[nodiscard]] Result before_open(const Owner&) noexcept;
    [[nodiscard]] Result observe_owner(const Owner&) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool shutdown(const Owner&) noexcept;
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool qualified_open() const noexcept;
    [[nodiscard]] bool owns_raw_claim() const noexcept;

private:
    struct Operation {
        std::atomic_flag& flag;
        ~Operation() { flag.clear(std::memory_order_release); }
    };
    [[nodiscard]] bool read(State&) const noexcept;
    [[nodiscard]] bool owner_matches(const Owner&, const State&) const noexcept;
    [[nodiscard]] bool same_window(const State&, const State&) const noexcept;
    [[nodiscard]] bool raw_matches(const graphics::input::RawInputSnapshot&,
                                   const State&) const noexcept;
    [[nodiscard]] Result restore(const Owner&, const State&) noexcept;
    [[nodiscard]] Result fallback(Result) noexcept;

    Bindings bindings_{};
    State admitted_{};
    graphics::input::RawCaptureClaim claim_{};
    bool cleared_{};
    std::atomic_flag operation_ = ATOMIC_FLAG_INIT;
    std::atomic_bool configured_{};
    std::atomic_bool accepting_{};
    std::atomic_bool openQualified_{};
    std::atomic_bool claimOwned_{};
};

} // namespace dawn::client::hooks::cursor::simple_tail
