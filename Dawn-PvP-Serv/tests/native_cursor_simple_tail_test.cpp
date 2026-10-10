#include "../dawn/src/client/hooks/cursor/native_cursor_simple_tail.h"

#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>
#include <vector>

#ifndef RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
#define RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL 0
#endif

namespace s = dawn::client::hooks::cursor::simple_tail;
namespace i = dawn::client::hooks::graphics::input;
namespace {
unsigned checks{};
void check(bool value, const char* reason) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}
struct Fixture {
    s::Policy policy;
    s::State state{};
    i::RawCaptureClaim rawClaim{};
    std::uint64_t rawBinding{50};
    unsigned callbacks{};
    bool guard{true}, readFailed{}, clearFailed{}, clipFailed{}, geometryFailed{}, releaseFailed{};
    bool closeInClear{}, recurseInClear{}, resizeInGeometry{}, markerChangeInGeometry{};
    bool reopenInClip{};
    RECT client{10, 20, 810, 620};
    unsigned fallbackRequests{}, clears{}, geometries{}, releases{};
    std::vector<unsigned> order;
    std::vector<RECT> clipped;
    std::vector<bool> rectangleRequests;
    s::Result recursive{s::Result::disabled};
    std::latch* clearEntered{};
    std::latch* clearReturns{};
    Fixture() {
        state.visibilityGeneration = 10;
        state.mainBindingGeneration = 20;
        state.windowRevision = 1;
        state.window = state.nativeWindow = reinterpret_cast<HWND>(std::uintptr_t{30});
        state.windowThread = 40;
        state.marker = 1;
        state.visibilityAvailable = state.mainBindingOwned = state.nativeContractQualified = true;
    }
    s::Owner owner() const { return {state.window, state.windowThread, true}; }
    void visible(bool value) { state.visible = value; ++state.visibilityGeneration; }
    static bool read(void* raw, s::State& value) noexcept {
        auto& f = *static_cast<Fixture*>(raw); value = f.state;
        SetLastError(ERROR_BUSY); return !f.readFailed;
    }
    static bool attached(void* raw) noexcept { return static_cast<Fixture*>(raw)->guard; }
    static void fallback(void* raw) noexcept { ++static_cast<Fixture*>(raw)->fallbackRequests; }
    static i::RawInputSnapshot snapshot(void* raw) noexcept {
        const auto& f = *static_cast<Fixture*>(raw);
        i::RawInputSnapshot value{};
        value.visibilityGeneration = f.state.visibilityGeneration;
        value.bindingGeneration = f.rawBinding;
        value.window = reinterpret_cast<HWND>(std::uintptr_t{60});
        value.activeCallbacks = f.callbacks;
        value.installed = value.stableVisibility = true;
        value.visible = f.state.visible;
        value.claim = f.rawClaim;
        value.claimMatched = f.rawClaim.identity != 0 && f.state.visible
            && f.rawClaim.visibilityGeneration == f.state.visibilityGeneration
            && f.rawClaim.bindingGeneration == f.rawBinding;
        value.drained = value.claimMatched && f.callbacks == 0;
        return value;
    }
    static bool claim(void* raw, std::uint64_t visible, std::uint64_t binding,
                      i::RawCaptureClaim& value) noexcept {
        auto& f = *static_cast<Fixture*>(raw);
        if (!f.state.visible || f.state.visibilityGeneration != visible || f.rawBinding != binding)
            return false;
        f.rawClaim = {71, visible, binding, reinterpret_cast<HWND>(std::uintptr_t{60})};
        value = f.rawClaim; f.order.push_back(1); return true;
    }
    static bool release(void* raw, const i::RawCaptureClaim& value) noexcept {
        auto& f = *static_cast<Fixture*>(raw); ++f.releases;
        if (f.releaseFailed || value != f.rawClaim) return false;
        f.order.push_back(4); f.rawClaim = {}; return true;
    }
    static bool clear(void* raw) noexcept {
        auto& f = *static_cast<Fixture*>(raw); ++f.clears; f.order.push_back(2);
        if (f.recurseInClear) f.recursive = f.policy.observe_owner(f.owner());
        if (f.closeInClear) f.visible(false);
        if (f.clearEntered) f.clearEntered->count_down();
        if (f.clearReturns) f.clearReturns->wait();
        return !f.clearFailed;
    }
    static bool geometry(void* raw, HWND window, RECT& rectangle) noexcept {
        auto& f = *static_cast<Fixture*>(raw); ++f.geometries;
        if (window != f.state.window || f.geometryFailed) return false;
        rectangle = f.client;
        if (f.resizeInGeometry) {
            f.client.right = 1510; ++f.state.windowRevision; f.resizeInGeometry = false;
        }
        if (f.markerChangeInGeometry) { f.state.marker = 0; f.markerChangeInGeometry = false; }
        return true;
    }
    static bool clip(void* raw, const RECT* bounds) noexcept {
        auto& f = *static_cast<Fixture*>(raw); f.order.push_back(3);
        f.rectangleRequests.push_back(bounds != nullptr);
        f.clipped.push_back(bounds ? *bounds : RECT{});
        if (f.reopenInClip) { f.visible(true); f.reopenInClip = false; }
        SetLastError(ERROR_ACCESS_DENIED);
        return !f.clipFailed;
    }
    s::Bindings bindings() {
        return {this, read, attached, fallback, snapshot, claim, release, clear, geometry, clip};
    }
    void configure() { check(policy.configure(true, bindings()), "configure explicit enabled prototype"); }
    void open() {
        check(policy.before_open(owner()) == s::Result::openingAllowed,
              "real guard confirmed before visibility transition");
        check(!state.visible, "preflight does not silently change Core");
        visible(true); state.primaryPauseOwned = true;
        check(policy.observe_owner(owner()) == s::Result::opened, "matched zero raw count opens");
    }
    void close() { visible(false); state.primaryPauseOwned = false; }
};
}

int main() {
#if !RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
    Fixture f;
    check(!f.policy.configure(true, f.bindings()), "disabled build cannot configure even opt-in");
    check(f.policy.before_open(f.owner()) == s::Result::disabled, "disabled preflight");
    check(f.policy.observe_owner(f.owner()) == s::Result::disabled, "disabled owner");
    check(f.policy.shutdown(f.owner()), "disabled shutdown safe");
    check(f.order.empty() && f.fallbackRequests == 0, "disabled build invokes no adapter");
#else
    {
        Fixture f;
        check(!f.policy.configure(false, f.bindings()), "default runtime opt-in false");
        auto broken = f.bindings(); broken.clearMouse = nullptr;
        check(!f.policy.configure(true, broken), "missing genuine mouse clear refuses configuration");
        f.configure(); f.guard = false;
        check(f.policy.before_open(f.owner()) == s::Result::waitingGuard, "unguarded open blocked");
        check(!f.state.visible && f.fallbackRequests == 1, "cannot flip visible then wait for old raw drain");
        auto foreign = f.owner(); ++foreign.thread;
        check(f.policy.before_open(foreign) == s::Result::waitingOwner, "foreign window worker cannot open");
        f.guard = true; f.callbacks = 2;
        check(f.policy.before_open(f.owner()) == s::Result::openingAllowed, "guarded transition permitted");
        f.visible(true); f.state.primaryPauseOwned = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingRawDrain, "old raw calls drain first");
        check(f.clears == 0 && f.clipped.empty() && !f.policy.qualified_open(), "no unguarded readiness credit");
        f.callbacks = 0;
        SetLastError(ERROR_NOT_READY);
        check(f.policy.observe_owner(f.owner()) == s::Result::opened, "first matched drain commits clear/release");
        check(GetLastError() == ERROR_NOT_READY, "owner operation preserves last error");
        check(f.order == std::vector<unsigned>({1, 2, 3}), "capture then drain/clear then release order");
        check(f.policy.qualified_open() && f.policy.owns_raw_claim(), "claim retained through visible epoch");
        f.guard = false; f.callbacks = 1; ++f.state.windowRevision;
        check(f.policy.observe_owner(f.owner()) == s::Result::released, "new captured callbacks need no repeated packet clear");
        check(f.clears == 1 && !f.rectangleRequests.back(), "tail unclips without active/request mutations");
        f.close(); f.state.marker = 0;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "native NULL intent restored");
        check(f.geometries == 0 && !f.rectangleRequests.back(), "NULL never becomes saved rectangle");
        check(!f.policy.owns_raw_claim() && f.rawClaim.identity == 0, "exact claim released only after close");
        check(f.policy.shutdown(f.owner()), "shutdown retires restored metadata");
    }
    {
        Fixture f; f.configure(); f.open();
        f.client = {50, 60, 1370, 820}; ++f.state.windowRevision; f.close();
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "rectangle close after resize");
        check(f.rectangleRequests.back() && f.clipped.back().left == 50
            && f.clipped.back().right == 1370, "close recomputes current geometry, never pre-open rectangle");
        check(f.state.marker == 1, "prototype does not modify native intent marker");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.resizeInGeometry = true;
        const auto count = f.clipped.size();
        check(f.policy.observe_owner(f.owner()) == s::Result::stale, "nested resize invalidates computed rectangle");
        check(f.clipped.size() == count && f.policy.owns_raw_claim(), "stale geometry not applied, ownership retained");
        check(f.policy.observe_owner(f.owner()) == s::Result::closed && f.clipped.back().right == 1510,
              "retry recomputes newly changed window geometry");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.markerChangeInGeometry = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::stale, "native NULL supersedes rectangle computation");
        check(f.policy.observe_owner(f.owner()) == s::Result::closed && !f.rectangleRequests.back(),
              "retry preserves superseding native NULL intent");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.geometryFailed = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::geometryFailed && f.policy.owns_raw_claim(),
              "geometry failure requests real fallback and retains close");
        f.geometryFailed = false; f.clipFailed = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::clipFailed && f.policy.owns_raw_claim(),
              "Clip failure retains addresses/claim for retry");
        f.clipFailed = false; f.releaseFailed = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::rawReleaseFailed && f.policy.owns_raw_claim(),
              "release failure does not retire exact raw metadata");
        f.releaseFailed = false;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "failed close remains retryable");
    }
    {
        Fixture f; f.configure(); f.recurseInClear = true; f.open();
        check(f.recursive == s::Result::busy, "native reentry rejected without deadlock or metadata reset");
        f.close(); f.reopenInClip = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::stale, "reopen during restore retains old claim");
        check(f.policy.owns_raw_claim() && !f.policy.qualified_open(), "reopen cannot inherit old omission readiness");
        f.close(); check(f.policy.observe_owner(f.owner()) == s::Result::closed, "later current close completes");
    }
    {
        Fixture f; f.configure(); f.closeInClear = true;
        check(f.policy.before_open(f.owner()) == s::Result::openingAllowed, "guarded open before injected close");
        f.visible(true); f.state.primaryPauseOwned = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::stale && f.policy.owns_raw_claim(),
              "visibility changes during native clear refuse ready state");
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "injected close safely restores own claim");
    }
    {
        Fixture f; f.configure(); f.open();
        ++f.rawBinding;
        check(f.policy.observe_owner(f.owner()) == s::Result::stale, "raw rebind invalidates capture proof");
        check(!f.policy.qualified_open() && f.policy.owns_raw_claim(), "rebind retains old metadata and requests fallback");
        f.close(); f.rawClaim.identity = 999;
        check(f.policy.observe_owner(f.owner()) == s::Result::rawReleaseFailed,
              "foreign replacement raw claim never cleared");
        check(f.rawClaim.identity == 999 && f.policy.owns_raw_claim(), "foreign claim preserved exactly");
    }
    {
        Fixture f; f.configure(); f.open(); f.close();
        f.state.marker = 2;
        check(f.policy.observe_owner(f.owner()) == s::Result::unknownNativeState,
              "unsupported marker is not guessed NULL or RECT");
        check(!f.policy.shutdown(f.owner()) && f.policy.configured(), "unknown close blocks metadata retirement");
        f.state.marker = 0;
        check(f.policy.shutdown(f.owner()), "supported close retry permits shutdown");
    }
    {
        Fixture f; f.configure(); f.open(); f.close();
        f.state.nativeWindow = reinterpret_cast<HWND>(std::uintptr_t{31});
        check(f.policy.observe_owner(f.owner()) == s::Result::unknownNativeState,
              "new native HWND never inherits old-window qualification");
        check(f.policy.owns_raw_claim() && f.fallbackRequests != 0,
              "window recreation keeps real fallback required");
    }
    {
        Fixture f; f.configure();
        std::latch entered{1}, returned{1}; f.clearEntered = &entered; f.clearReturns = &returned;
        check(f.policy.before_open(f.owner()) == s::Result::openingAllowed, "race preflight guarded");
        f.visible(true); f.state.primaryPauseOwned = true;
        s::Result workerResult{};
        std::thread worker{[&] { workerResult = f.policy.observe_owner(f.owner()); }};
        entered.wait();
        f.policy.stop();
        check(!f.policy.shutdown(f.owner()), "shutdown cannot retire blocked native-call addresses");
        check(f.policy.owns_raw_claim() && !f.policy.qualified_open(), "cancellation preserves old claim");
        returned.count_down(); worker.join();
        check(workerResult == s::Result::stale, "stopped in-flight clear cannot publish ready afterward");
        f.close();
        check(f.policy.shutdown(f.owner()), "drained cancelled operation restores before retirement");
    }
#endif
    std::printf("PASS %u default-off native cursor simple-tail prototype checks; no game access\n", checks);
}
