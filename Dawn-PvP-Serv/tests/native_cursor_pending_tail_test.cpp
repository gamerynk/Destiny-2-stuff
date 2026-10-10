#include "../dawn/src/client/hooks/cursor/native_cursor_pending_tail.h"

#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>
#include <vector>

#ifndef RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
#define RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL 0
#endif

namespace s = dawn::client::hooks::cursor::pending_tail;
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
    i::RawOpeningLease rawClaim{};
    std::uint64_t rawBinding{50};
    std::uint64_t sequence{70};
    unsigned callbacks{};
    bool retired{}, commitFails{}, closeFails{},  readFailed{}, clearFailed{}, clipFailed{}, geometryFailed{}, releaseFailed{};
    bool guard{};
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
    static bool guarded_read(void* raw, s::State& value) noexcept {
        value = static_cast<Fixture*>(raw)->state; return true;
    }
    static bool attached(void* raw) noexcept { return static_cast<Fixture*>(raw)->guard; }
    static void fallback(void* raw) noexcept { ++static_cast<Fixture*>(raw)->fallbackRequests; }
    static i::RawInputSnapshot binding(void* raw) noexcept {
        auto& f = *static_cast<Fixture*>(raw); i::RawInputSnapshot v{};
        v.installed = true; v.bindingGeneration = f.rawBinding; return v;
    }
    static i::RawOpeningSnapshot snapshot(void* raw, const i::RawOpeningLease& lease) noexcept {
        const auto& f = *static_cast<Fixture*>(raw); i::RawOpeningSnapshot v{};
        v.lease = f.rawClaim; v.activeCallbacks = f.callbacks;
        v.matched = lease.identity && lease == f.rawClaim && lease.bindingGeneration == f.rawBinding;
        v.retired = f.retired || lease.bindingGeneration != f.rawBinding;
        v.drained = v.matched && !v.activeCallbacks; return v;
    }
    static bool claim(void* raw, std::uint64_t hidden, std::uint64_t binding,
                      i::RawOpeningLease& value) noexcept {
        auto& f = *static_cast<Fixture*>(raw);
        if (f.state.visible || f.state.visibilityGeneration != hidden || f.rawBinding != binding
            || f.rawClaim.identity) return false;
        f.rawClaim = {++f.sequence, hidden, binding, reinterpret_cast<HWND>(std::uintptr_t{60})};
        value = f.rawClaim; f.order.push_back(1); return true;
    }
    static std::uint64_t commit_open(void* raw, UINT key, std::uint64_t hidden) noexcept {
        auto& f = *static_cast<Fixture*>(raw);
        if (f.commitFails || key != VK_INSERT || f.state.visible
            || f.state.visibilityGeneration != hidden) return 0;
        f.visible(true); f.order.push_back(5); return f.state.visibilityGeneration;
    }
    static bool commit_close(void* raw, UINT key, std::uint64_t visible) noexcept {
        auto& f = *static_cast<Fixture*>(raw);
        if (f.closeFails || key != VK_INSERT || !f.state.visible
            || f.state.visibilityGeneration != visible) return false;
        f.visible(false); f.order.push_back(6); return true;
    }
    static void apply(void* raw, bool visible) noexcept {
        auto& f = *static_cast<Fixture*>(raw); f.state.primaryPauseOwned = visible;
    }
    static bool release(void* raw, const i::RawOpeningLease& value) noexcept {
        auto& f = *static_cast<Fixture*>(raw); ++f.releases;
        if (f.releaseFailed || value != f.rawClaim) return false;
        f.order.push_back(4); f.rawClaim = {}; return true;
    }
    static bool clear(void* raw) noexcept {
        auto& f = *static_cast<Fixture*>(raw); ++f.clears; f.order.push_back(2);
        if (f.recurseInClear) f.recursive = f.policy.observe_owner(f.owner());
        if (f.closeInClear) f.visible(false);
        const auto entered = f.clearEntered; f.clearEntered = nullptr;
        const auto returned = f.clearReturns; f.clearReturns = nullptr;
        if (entered) entered->count_down();
        if (returned) returned->wait();
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
        return {this, read, guarded_read, claim, snapshot, release, binding, clear, commit_open, commit_close,
                apply, geometry, clip, fallback, apply, attached};
    }
    void configure() { check(policy.configure(true, bindings()), "configure enabled pending route"); }
    void open() {
        check(policy.request_toggle(VK_INSERT) == s::Result::requested, "record toggle before owner");
        check(!state.visible && !rawClaim.identity, "key admission performs no visibility/raw/native operation");
        check(policy.observe_owner(owner()) == s::Result::opened, "drained pre-visible lease opens");
    }
    void close() { visible(false); state.primaryPauseOwned = false; }

};
}


int main() {
#if !RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL
    Fixture f;
    check(!f.policy.configure(true, f.bindings()), "default-off build cannot configure");
    check(f.policy.request_toggle(VK_INSERT) == s::Result::disabled, "default-off key path");
    check(f.policy.observe_owner(f.owner()) == s::Result::disabled, "default-off owner path");
    check(f.order.empty(), "default-off executes no adapter");
#else
    {
        Fixture f; f.configure(); f.callbacks = 1;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "pending request");
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain, "old native raw handler blocks commit");
        check(!f.state.visible && f.rawClaim.identity && !f.clears, "hidden capture precedes clear and Core");
        f.callbacks = 0; SetLastError(1453);
        check(f.policy.observe_owner(f.owner()) == s::Result::opened, "old raw drain permits Core commit");
        check(GetLastError() == 1453, "last-error preserved");
        check(f.order == std::vector<unsigned>({1,2,5,3}), "claim/drain/clear/Core/unclip exact order");
        const auto identity = f.rawClaim.identity;
        check(f.state.visible && f.policy.owns_capture() && !f.policy.pending(), "continuous capture after visible commit");
        f.callbacks = 2;
        check(f.policy.observe_owner(f.owner()) == s::Result::released, "new captured callbacks do not require another old drain");
        check(f.clears == 1 && f.rawClaim.identity == identity, "no repeated clear or lease replacement");
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "close recorded");
        f.state.marker = 0;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "owner closes Core then restores NULL");
        check(!f.state.visible && !f.policy.owns_capture() && !f.rawClaim.identity, "permission retires after restoration");
        check(!f.geometries && !f.rectangleRequests.back(), "native NULL never becomes RECT");
        check(f.policy.shutdown(f.owner()), "clean shutdown");
    }
    {
        Fixture f; f.configure(); f.callbacks = 1;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "pending cancel request starts");
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain, "pending cancel old admission retained");
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "second toggle cancels pending open");
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain && !f.state.visible,
              "cancel cannot resume before old handler drain");
        f.callbacks = 0;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "cancel clears old packet/restores/releases");
        check(f.clears == 1 && !f.state.visible && !f.policy.owns_capture(), "cancel never exposes UI");
    }
    {
        Fixture f; f.configure(); f.open(); f.close();
        f.client = {100,200,1500,1000};
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "closed current RECT restoration");
        check(f.clipped.back().left == 100 && f.clipped.back().right == 1500,
              "fresh resized geometry used");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.resizeInGeometry = true;
        const auto count = f.clipped.size();
        check(f.policy.observe_owner(f.owner()) == s::Result::stale, "native geometry reentry invalidates old rectangle");
        check(f.clipped.size() == count && f.policy.owns_capture(), "stale result cannot release capture");
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "resize retry succeeds");
        check(f.clipped.back().right == 1510, "retry samples new geometry");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.releaseFailed = true;
        check(f.policy.observe_owner(f.owner()) == s::Result::rawReleaseFailed, "exact release failure retained");
        check(f.policy.owns_capture(), "cannot retire live permission");
        f.releaseFailed = false;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed, "release retry completes");
    }
    {
        Fixture f; f.configure(); f.callbacks = 1;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "retirement fixture pending");
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain, "retirement fixture old call alive");
        f.policy.stop(); f.retired = true; f.rawClaim = {};
        check(!f.policy.shutdown(f.owner()), "retirement never resets old callback count");
        f.callbacks = 0;
        check(f.policy.shutdown(f.owner()), "retired exact binding/aggregate-zero allows obsolete metadata cleanup");
        check(f.clears == 1, "late packet still cleared on cancelled old binding");
    }
    {
        Fixture f; f.configure(); f.recurseInClear = true; f.open();
        check(f.recursive == s::Result::busy, "nested native callback never re-enters an active policy operation");
    }
    {
        Fixture f; f.configure(); f.commitFails = true;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "failed commit request");
        check(f.policy.observe_owner(f.owner()) == s::Result::commitFailed, "conditional Core failure retained");
        check(!f.state.visible && f.policy.owns_capture(), "no blind toggle after foreign Core transition");
        f.commitFails = false;
        check(f.policy.observe_owner(f.owner()) == s::Result::opened, "conditional retry resumes same capture");
        check(f.clears == 1, "same continuously captured permission does not need repeated clear");
    }
    {
        Fixture f; f.configure(); f.open(); f.close(); f.rawClaim.identity = 999;
        check(f.policy.observe_owner(f.owner()) == s::Result::rawReleaseFailed, "replacement permission cannot be released");
        check(f.rawClaim.identity == 999 && f.policy.owns_capture(), "foreign permission preserved");
    }
    {
        Fixture f; f.configure(); f.open();
        check(!f.policy.shutdown(f.owner()), "visible shutdown cannot release capture or retire adapters");
        check(f.state.visible && f.policy.owns_capture(), "visible shutdown retains real input protection");
        check(f.policy.suspend_window(f.owner()), "actual owner hides before native window retirement");
        check(!f.state.visible && f.policy.owns_capture(), "retirement hides UI without prematurely retiring raw lease");
        check(f.policy.shutdown(f.owner()), "owner close/drain permits retryable shutdown");
        f.configure(); f.open(); f.close();
        check(f.policy.shutdown(f.owner()), "same-module second configure/open/close/shutdown succeeds");
    }
    {
        Fixture f; f.configure(); f.callbacks = 1;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "guarded fallback pending opening");
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain, "fallback owns old capture before loss");
        f.readFailed = true;
        check(!f.policy.handoff_guarded(f.owner(), true), "guard request alone is not actual attachment");
        f.guard = true;
        check(!f.policy.handoff_guarded(f.owner(), true), "actual guard cannot drop old native handler early");
        check(f.policy.owns_capture() && !f.state.visible && !f.clears, "guard handoff waits real drain");
        f.callbacks = 0;
        check(f.policy.intent_open(), "pending open intent retained for legacy route");
        check(f.policy.handoff_guarded(f.owner(), true), "real guard plus drain restores finite legacy handoff despite native metadata loss");
        check(!f.policy.configured() && !f.policy.owns_capture() && !f.rawClaim.identity,
              "handoff retires adapters only after old permission release");
        check(f.policy.request_toggle(VK_INSERT) == s::Result::disabled,
              "retired native policy no longer consumes normal Core toggles");
    }
    {
        Fixture f; f.configure(); f.open();
        check(f.policy.suspend_window(f.owner()), "native recreation starts with hidden UI");
        f.state.window = f.state.nativeWindow = reinterpret_cast<HWND>(std::uintptr_t{90});
        ++f.state.mainBindingGeneration; ++f.state.windowRevision;
        f.callbacks = 1;
        check(f.policy.observe_owner(f.owner()) == s::Result::waitingDrain,
              "new qualified owner cannot inherit old UI while admitted raw writer remains");
        check(!f.state.visible && f.policy.owns_capture(), "recreated window setup remains genuinely hidden");
        f.callbacks = 0;
        check(f.policy.observe_owner(f.owner()) == s::Result::closed,
              "new installed binding finishes old capture drain/clear/current restoration");
        check(!f.policy.owns_capture() && !f.state.visible, "recreation does not blindly carry visible old generation");
        f.open();
        check(f.state.visible && f.clears == 3, "intended reopen must acquire/drain its own fresh hidden lease");
    }
    {
        Fixture f; f.configure();
        std::latch entered{1}, returned{1}; f.clearEntered = &entered; f.clearReturns = &returned;
        check(f.policy.request_toggle(VK_INSERT) == s::Result::requested, "in-flight fixture request");
        s::Result result{};
        std::thread worker{[&] { result = f.policy.observe_owner(f.owner()); }};
        entered.wait(); f.policy.stop();
        check(!f.policy.shutdown(f.owner()), "unload does not retire blocked native-clear metadata");
        check(!f.state.visible && f.policy.owns_capture(), "Core remains hidden while old-clear operation held");
        returned.count_down(); worker.join();
        check(result == s::Result::stale && !f.state.visible, "stop blocks late Core commit");
        check(f.policy.shutdown(f.owner()), "cancelled native operation drained cleanup succeeds");
    }
#endif
    std::printf("PASS %u pre-visible cursor pending-tail checks; no game access\n", checks);
}
