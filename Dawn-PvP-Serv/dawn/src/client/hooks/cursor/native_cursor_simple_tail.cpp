#include "native_cursor_simple_tail.h"

#ifndef RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
#define RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL 0
#endif

namespace dawn::client::hooks::cursor::simple_tail {
namespace {
struct LastError {
    DWORD value{GetLastError()};
    ~LastError() { SetLastError(value); }
};
}

bool Policy::configure(bool optIn, const Bindings& bindings) noexcept {
    const LastError error;
    if (!RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL || !optIn) return false;
    if (operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    if (configured() || owns_raw_claim() || !bindings.readState || !bindings.guardAttached
        || !bindings.requestGuard || !bindings.rawSnapshot || !bindings.claimRaw
        || !bindings.releaseRaw || !bindings.clearMouse || !bindings.currentClientRectangle
        || !bindings.clip) return false;
    bindings_ = bindings;
    admitted_ = {};
    claim_ = {};
    cleared_ = false;
    accepting_.store(true, std::memory_order_release);
    configured_.store(true, std::memory_order_release);
    return true;
}

bool Policy::configured() const noexcept { return configured_.load(std::memory_order_acquire); }
bool Policy::qualified_open() const noexcept { return openQualified_.load(std::memory_order_acquire); }
bool Policy::owns_raw_claim() const noexcept { return claimOwned_.load(std::memory_order_acquire); }
void Policy::stop() noexcept {
    accepting_.store(false, std::memory_order_release);
    openQualified_.store(false, std::memory_order_release);
}

bool Policy::read(State& state) const noexcept {
    return bindings_.readState(bindings_.context, state) && state.visibilityGeneration != 0
        && state.mainBindingGeneration != 0 && state.windowRevision != 0 && state.window != nullptr
        && state.nativeWindow == state.window && state.windowThread != 0
        && state.mainBindingOwned && state.nativeContractQualified && state.marker <= 1;
}
bool Policy::owner_matches(const Owner& owner, const State& state) const noexcept {
    return owner.outsideNativeOwnership && owner.window == state.window
        && owner.thread == state.windowThread;
}
bool Policy::same_window(const State& left, const State& right) const noexcept {
    return left.window == right.window && left.nativeWindow == right.nativeWindow
        && left.windowThread == right.windowThread
        && left.mainBindingGeneration == right.mainBindingGeneration;
}
bool Policy::raw_matches(const graphics::input::RawInputSnapshot& raw,
                         const State& state) const noexcept {
    return raw.installed && raw.stableVisibility && raw.visible && raw.claimMatched
        && raw.visibilityGeneration == state.visibilityGeneration
        && raw.bindingGeneration == claim_.bindingGeneration && raw.window == claim_.window
        && raw.claim == claim_;
}
Result Policy::fallback(Result result) noexcept {
    openQualified_.store(false, std::memory_order_release);
    bindings_.requestGuard(bindings_.context);
    return result;
}

Result Policy::before_open(const Owner& owner) noexcept {
    const LastError error;
    if (!configured() || !accepting_.load(std::memory_order_acquire)) return Result::disabled;
    if (operation_.test_and_set(std::memory_order_acquire)) return Result::busy;
    const Operation operation{operation_};
    openQualified_.store(false, std::memory_order_release);
    State state;
    if (!read(state)) return fallback(Result::unknownNativeState);
    if (!owner_matches(owner, state)) return fallback(Result::waitingOwner);
    if (state.visible || owns_raw_claim()) return fallback(Result::stale);
    // a visible-generation claim starts after Core commits and cannot fence older native raw
    // calls; never authorize a toggle without the real guard
    if (!bindings_.guardAttached(bindings_.context)) return fallback(Result::waitingGuard);
    if (!state.visibilityAvailable) return fallback(Result::unknownNativeState);
    State after;
    if (!accepting_.load(std::memory_order_acquire) || !read(after)
        || !owner_matches(owner, after) || !same_window(after, state)
        || after.windowRevision != state.windowRevision || after.visible
        || after.visibilityGeneration != state.visibilityGeneration)
        return fallback(Result::stale);
    return Result::openingAllowed;
}

Result Policy::restore(const Owner& owner, const State& state) noexcept {
    if (!owner_matches(owner, state)) return fallback(Result::waitingOwner);
    if (!same_window(state, admitted_)) return fallback(Result::stale);
    RECT rectangle{};
    if (state.marker != 0
        && !bindings_.currentClientRectangle(bindings_.context, state.window, rectangle))
        return fallback(Result::geometryFailed);
    State after;
    if (!read(after) || !owner_matches(owner, after) || !same_window(after, state)
        || after.visible || after.marker != state.marker
        || after.windowRevision != state.windowRevision
        || after.visibilityGeneration != state.visibilityGeneration) return fallback(Result::stale);
    if (!bindings_.clip(bindings_.context, state.marker ? &rectangle : nullptr))
        return fallback(Result::clipFailed);
    if (!read(after) || !owner_matches(owner, after) || !same_window(after, state)
        || after.visible || after.marker != state.marker
        || after.windowRevision != state.windowRevision
        || after.visibilityGeneration != state.visibilityGeneration) return fallback(Result::stale);
    if (!bindings_.releaseRaw(bindings_.context, claim_)) return fallback(Result::rawReleaseFailed);
    claim_ = {};
    admitted_ = {};
    cleared_ = false;
    claimOwned_.store(false, std::memory_order_release);
    return Result::closed;
}

Result Policy::observe_owner(const Owner& owner) noexcept {
    const LastError error;
    if (!configured()) return Result::disabled;
    if (operation_.test_and_set(std::memory_order_acquire)) return Result::busy;
    const Operation operation{operation_};
    State state;
    if (!read(state)) return fallback(Result::unknownNativeState);
    if (!owner_matches(owner, state)) return fallback(Result::waitingOwner);
    if (!state.visible) {
        openQualified_.store(false, std::memory_order_release);
        return owns_raw_claim() ? restore(owner, state) : Result::closed;
    }
    if (!accepting_.load(std::memory_order_acquire)) return fallback(Result::stale);
    if (!state.visibilityAvailable || !state.primaryPauseOwned)
        return fallback(Result::unknownNativeState);
    if (!owns_raw_claim()) {
        if (!bindings_.guardAttached(bindings_.context)) return fallback(Result::waitingGuard);
        const auto raw = bindings_.rawSnapshot(bindings_.context);
        if (!raw.installed || !raw.stableVisibility || !raw.visible
            || raw.visibilityGeneration != state.visibilityGeneration)
            return fallback(Result::waitingRawDrain);
        if (!bindings_.claimRaw(bindings_.context, state.visibilityGeneration,
                               raw.bindingGeneration, claim_)) return fallback(Result::stale);
        admitted_ = state;
        cleared_ = false;
        claimOwned_.store(true, std::memory_order_release);
    }
    if (!same_window(state, admitted_)
        || state.visibilityGeneration != admitted_.visibilityGeneration)
        return fallback(Result::stale);
    if (!accepting_.load(std::memory_order_acquire)) return fallback(Result::stale);
    auto raw = bindings_.rawSnapshot(bindings_.context);
    if (!raw_matches(raw, state)) return fallback(Result::stale);
    if (!cleared_) {
        if (!raw.drained || raw.activeCallbacks != 0) return fallback(Result::waitingRawDrain);
        if (!bindings_.clearMouse(bindings_.context)) return fallback(Result::clearFailed);
        State after;
        if (!accepting_.load(std::memory_order_acquire) || !read(after)
            || !owner_matches(owner, after) || !same_window(after, state)
            || !after.visible || after.visibilityGeneration != state.visibilityGeneration
            || after.windowRevision != state.windowRevision
            || !after.primaryPauseOwned) return fallback(Result::stale);
        raw = bindings_.rawSnapshot(bindings_.context);
        if (!raw_matches(raw, after)) return fallback(Result::stale);
        cleared_ = true;
    }
    if (!bindings_.clip(bindings_.context, nullptr)) return fallback(Result::clipFailed);
    State after;
    if (!accepting_.load(std::memory_order_acquire) || !read(after)
        || !owner_matches(owner, after) || !same_window(after, state)
        || !after.visible || after.visibilityGeneration != state.visibilityGeneration
        || after.windowRevision != state.windowRevision
        || !after.primaryPauseOwned
        || !raw_matches(bindings_.rawSnapshot(bindings_.context), after))
        return fallback(Result::stale);
    const bool already = qualified_open();
    openQualified_.store(true, std::memory_order_release);
    return already ? Result::released : Result::opened;
}

bool Policy::shutdown(const Owner& owner) noexcept {
    const LastError error;
    stop();
    if (!configured()) return true;
    if (operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    if (owns_raw_claim()) {
        State state;
        if (!read(state) || state.visible || restore(owner, state) != Result::closed) {
            (void)fallback(Result::stale);
            return false;
        }
    }
    bindings_ = {};
    admitted_ = {};
    configured_.store(false, std::memory_order_release);
    return true;
}

} // namespace dawn::client::hooks::cursor::simple_tail
