#include "native_cursor_pending_tail.h"
#ifndef RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
#define RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL 0
#endif
namespace dawn::client::hooks::cursor::pending_tail {
namespace { struct LastError { DWORD value{GetLastError()}; ~LastError() { SetLastError(value); } }; }

bool Policy::configure(bool optIn, const Bindings& b) noexcept {
    const LastError error;
    if (!RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL || !optIn
        || operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    if (configured() || owns_capture() || !b.readState || !b.readGuardedState || !b.claimRaw || !b.rawSnapshot
        || !b.releaseRaw || !b.rawBinding || !b.clearMouse || !b.commitOpen || !b.commitClose
        || !b.applyInputVisibility || !b.currentClientRectangle || !b.clip || !b.requestGuard
        || !b.prepareGuardedVisibility || !b.guardAttached)
        return false;
    bindings_ = b;
    admitted_ = {}; lease_ = {}; key_ = 0; committedGeneration_ = 0;
    cleared_ = false; closeRequested_ = false;
    pending_.store(false, std::memory_order_release);
    intentOpen_.store(false, std::memory_order_release);
    accepting_.store(true, std::memory_order_release);
    configured_.store(true, std::memory_order_release);
    return true;
}
bool Policy::configured() const noexcept { return configured_.load(std::memory_order_acquire); }
bool Policy::owns_capture() const noexcept { return captureOwned_.load(std::memory_order_acquire); }
bool Policy::pending() const noexcept { return pending_.load(std::memory_order_acquire); }
bool Policy::intent_open() const noexcept { return intentOpen_.load(std::memory_order_acquire); }
void Policy::stop() noexcept { accepting_.store(false, std::memory_order_release); }
bool Policy::read(State& s) const noexcept {
    return bindings_.readState(bindings_.context, s) && s.visibilityAvailable
        && s.visibilityGeneration && s.mainBindingGeneration && s.windowRevision
        && s.window && s.nativeWindow == s.window && s.windowThread
        && s.mainBindingOwned && s.nativeContractQualified && s.marker <= 1;
}
bool Policy::owned(const Owner& o, const State& s) const noexcept {
    return o.outsideNativeOwnership && o.window == s.window && o.thread == s.windowThread;
}
bool Policy::same_window(const State& a, const State& b) const noexcept {
    return a.window == b.window && a.nativeWindow == b.nativeWindow
        && a.windowThread == b.windowThread && a.mainBindingGeneration == b.mainBindingGeneration;
}
bool Policy::unchanged(const Owner& o, const State& s, bool visible) const noexcept {
    State after;
    return read(after) && owned(o, after) && same_window(s, after)
        && after.visible == visible && after.windowRevision == s.windowRevision
        && after.visibilityGeneration == s.visibilityGeneration;
}
Result Policy::fallback(Result r) noexcept { bindings_.requestGuard(bindings_.context); return r; }

Result Policy::request_toggle(UINT key) noexcept {
    const LastError error;
    if (!configured() || !accepting_.load(std::memory_order_acquire)) return Result::disabled;
    if (operation_.test_and_set(std::memory_order_acquire)) return Result::busy;
    const Operation operation{operation_};
    if (!key) return Result::stale;
    if (pending()) {
        closeRequested_ = true;
        intentOpen_.store(false, std::memory_order_release);
    } else {
        key_ = key;
        closeRequested_ = committedGeneration_ != 0;
        intentOpen_.store(!closeRequested_, std::memory_order_release);
        pending_.store(true, std::memory_order_release);
    }
    return Result::requested;
}

Result Policy::cancel(const Owner& owner, const State& s) noexcept {
    if (owns_capture()) {
        if (!same_window(s, admitted_)) return fallback(Result::stale);
        const auto raw = bindings_.rawSnapshot(bindings_.context, lease_);
        if (!raw.matched && !raw.retired) return fallback(Result::stale);
        if (raw.activeCallbacks != 0 || (!raw.drained && !raw.retired)) return Result::waitingDrain;
        // even an aborted pending open must clear a late old native button writer before resume
        if (!cleared_) {
            if (!bindings_.clearMouse(bindings_.context)) return fallback(Result::clearFailed);
            if (!unchanged(owner, s, false)) return fallback(Result::stale);
            cleared_ = true;
        }
        return close(owner, s);
    }
    pending_.store(false, std::memory_order_release);
    closeRequested_ = false; key_ = 0;
    return Result::cancelled;
}

Result Policy::close(const Owner& owner, const State& s) noexcept {
    if (!same_window(s, admitted_)) return fallback(Result::stale);
    RECT rectangle{};
    if (s.marker && !bindings_.currentClientRectangle(bindings_.context, s.window, rectangle))
        return fallback(Result::geometryFailed);
    State after;
    if (!read(after) || !owned(owner, after) || !same_window(after, s)
        || after.visible || after.marker != s.marker || after.windowRevision != s.windowRevision
        || after.visibilityGeneration != s.visibilityGeneration) return fallback(Result::stale);
    if (!bindings_.clip(bindings_.context, s.marker ? &rectangle : nullptr))
        return fallback(Result::clipFailed);
    if (!read(after) || !owned(owner, after) || !same_window(after, s)
        || after.visible || after.marker != s.marker || after.windowRevision != s.windowRevision
        || after.visibilityGeneration != s.visibilityGeneration) return fallback(Result::stale);
    if (!bindings_.releaseRaw(bindings_.context, lease_)) {
        const auto raw = bindings_.rawSnapshot(bindings_.context, lease_);
        if (!raw.retired || raw.activeCallbacks != 0) return fallback(Result::rawReleaseFailed);
    }
    lease_ = {}; admitted_ = {}; cleared_ = false; closeRequested_ = false; key_ = 0;
    committedGeneration_ = 0;
    captureOwned_.store(false, std::memory_order_release);
    intentOpen_.store(false, std::memory_order_release);
    pending_.store(false, std::memory_order_release);
    return Result::closed;
}

Result Policy::observe_owner(const Owner& owner) noexcept {
    const LastError error;
    if (!configured()) return Result::disabled;
    if (operation_.test_and_set(std::memory_order_acquire)) return Result::busy;
    const Operation operation{operation_};
    State s;
    if (!read(s)) return fallback(Result::stale);
    if (!owned(owner, s)) return Result::waitingOwner;
    if (owns_capture() && !same_window(s, admitted_)) {
        // main generations never reset and replacement needs old-main callback count zero; a new
        // qualified owner may finish the old capture's drain but never inherit its UI
        if (s.visible || s.mainBindingGeneration <= admitted_.mainBindingGeneration)
            return fallback(Result::stale);
        admitted_ = s;
        committedGeneration_ = 0;
        closeRequested_ = true;
        pending_.store(true, std::memory_order_release);
        cleared_ = false;
    }
    if (!accepting_.load(std::memory_order_acquire)) {
        if (s.visible) return fallback(Result::stale);
        return cancel(owner, s);
    }
    if (committedGeneration_ != 0) {
        if (!s.visible) return close(owner, s);
        if (s.visibilityGeneration != committedGeneration_) return fallback(Result::stale);
        if (closeRequested_) {
            if (!bindings_.commitClose(bindings_.context, key_, committedGeneration_))
                return fallback(Result::commitFailed);
            bindings_.applyInputVisibility(bindings_.context, false);
            if (!read(s) || !owned(owner, s) || s.visible || !same_window(s, admitted_))
                return fallback(Result::stale);
            return close(owner, s);
        }
        const auto raw = bindings_.rawSnapshot(bindings_.context, lease_);
        if (!raw.matched || !s.primaryPauseOwned) return fallback(Result::stale);
        if (!bindings_.clip(bindings_.context, nullptr)) return fallback(Result::clipFailed);
        return unchanged(owner, s, true) ? Result::released : fallback(Result::stale);
    }
    if (!pending()) return s.visible ? fallback(Result::stale) : Result::idle;
    if (s.visible) return fallback(Result::stale);
    if (closeRequested_) return cancel(owner, s);
    if (!owns_capture()) {
        const auto binding = bindings_.rawBinding(bindings_.context);
        if (!binding.installed || !binding.bindingGeneration
            || !bindings_.claimRaw(bindings_.context, s.visibilityGeneration,
                                    binding.bindingGeneration, lease_)) return fallback(Result::stale);
        admitted_ = s;
        captureOwned_.store(true, std::memory_order_release);
    }
    if (s.visibilityGeneration != admitted_.visibilityGeneration) return cancel(owner, s);
    const auto raw = bindings_.rawSnapshot(bindings_.context, lease_);
    if (!raw.matched) return fallback(Result::stale);
    if (!cleared_) {
        if (!raw.drained) return Result::waitingDrain;
        if (!bindings_.clearMouse(bindings_.context)) return fallback(Result::clearFailed);
        if (!accepting_.load(std::memory_order_acquire) || !unchanged(owner, s, false)
            || !bindings_.rawSnapshot(bindings_.context, lease_).matched) return fallback(Result::stale);
        cleared_ = true;
    }
    if (!accepting_.load(std::memory_order_acquire)) return fallback(Result::stale);
    const auto committed = bindings_.commitOpen(bindings_.context, key_, s.visibilityGeneration);
    if (!committed) return fallback(Result::commitFailed);
    committedGeneration_ = committed;
    bindings_.applyInputVisibility(bindings_.context, true);
    if (!read(s) || !owned(owner, s) || !s.visible || !same_window(s, admitted_)
        || !bindings_.rawSnapshot(bindings_.context, lease_).matched) return fallback(Result::stale);
    if (s.visibilityGeneration != committedGeneration_) return fallback(Result::stale);
    pending_.store(false, std::memory_order_release);
    if (!s.primaryPauseOwned) return fallback(Result::stale);
    if (!bindings_.clip(bindings_.context, nullptr)) return fallback(Result::clipFailed);
    return unchanged(owner, s, true) ? Result::opened : fallback(Result::stale);
}

bool Policy::suspend_window(const Owner& owner) noexcept {
    const LastError error;
    if (!configured()) return true;
    if (operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    State s;
    if (!read(s) || !owned(owner, s)) { (void)fallback(Result::waitingOwner); return false; }
    if (s.visible) {
        if (!bindings_.commitClose(bindings_.context, key_, s.visibilityGeneration)) {
            (void)fallback(Result::commitFailed); return false;
        }
        bindings_.applyInputVisibility(bindings_.context, false);
    }
    committedGeneration_ = 0; closeRequested_ = true;
    pending_.store(owns_capture(), std::memory_order_release);
    return true;
}

bool Policy::handoff_guarded(const Owner& owner, bool intendedVisible) noexcept {
    const LastError error;
    if (!configured()) return true;
    if (operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    State s;
    const auto basic = [&](State& value) noexcept {
        return bindings_.readGuardedState(bindings_.context, value) && value.visibilityAvailable
            && value.visibilityGeneration && value.mainBindingGeneration && value.windowRevision
            && value.mainBindingOwned && value.window && value.windowThread && owned(owner, value);
    };
    if (!bindings_.guardAttached(bindings_.context)) return false;
    if (!basic(s)) return false;
    if (owns_capture()) {
        const auto raw = bindings_.rawSnapshot(bindings_.context, lease_);
        if ((!raw.matched && !raw.retired) || raw.activeCallbacks != 0) return false;
        if (!cleared_ && !bindings_.clearMouse(bindings_.context)) return false;
        bindings_.prepareGuardedVisibility(bindings_.context, intendedVisible);
        State after;
        if (!basic(after) || after.window != s.window || after.windowThread != s.windowThread
            || after.mainBindingGeneration != s.mainBindingGeneration
            || after.windowRevision != s.windowRevision || after.visible != s.visible
            || after.visibilityGeneration != s.visibilityGeneration) return false;
        if (!bindings_.releaseRaw(bindings_.context, lease_)) {
            const auto retired = bindings_.rawSnapshot(bindings_.context, lease_);
            if (!retired.retired || retired.activeCallbacks != 0) return false;
        }
    }
    if (!owns_capture()) bindings_.prepareGuardedVisibility(bindings_.context, intendedVisible);
    lease_ = {}; admitted_ = {}; cleared_ = false; closeRequested_ = false; key_ = 0;
    committedGeneration_ = 0; bindings_ = {};
    captureOwned_.store(false, std::memory_order_release);
    pending_.store(false, std::memory_order_release);
    accepting_.store(false, std::memory_order_release);
    configured_.store(false, std::memory_order_release);
    return true;
}

bool Policy::shutdown(const Owner& owner) noexcept {
    const LastError error;
    stop();
    if (!configured()) return true;
    const auto result = observe_owner(owner);
    if (result != Result::cancelled && result != Result::closed && result != Result::idle) return false;
    if (operation_.test_and_set(std::memory_order_acquire)) return false;
    const Operation operation{operation_};
    if (owns_capture()) return false;
    bindings_ = {}; admitted_ = {}; key_ = 0; committedGeneration_ = 0;
    configured_.store(false, std::memory_order_release);
    return true;
}
}
