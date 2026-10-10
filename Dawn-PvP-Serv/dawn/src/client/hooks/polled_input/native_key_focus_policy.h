#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace dawn::client::hooks::polled_input::native_key {

// native addresses are validated before selection; no callback runs under the metadata lock
struct Bindings {
    HWND window{};
    DWORD windowThread{};
    void* object{};
    volatile unsigned char* skipFocus{};
    const volatile unsigned char* windowFocused{};
    const volatile unsigned char* changeSuppressed{};
    const volatile unsigned char* primary{};
    const volatile unsigned* focus{};
    unsigned queueId{};
    void (*queueEnter)(unsigned, int){};
    void (*queueLeave)(unsigned, int){};
    void (*setPrimary)(bool){};
    void (*publishFocus)(void*, HWND, bool){};
    bool (*foregroundMatches)(HWND){};
    bool (*modal)(){};
    void (*observeFocus)(){};
    bool (*currentWindowOwner)(HWND){};
    bool (*realGuardAttached)(){};
    void (*requestRealGuard)(){};
};

class Policy final {
    struct Publisher {
        Publisher* previous{};
        bool queueOwned{};
    };
    struct Local {
        Policy* policy{};
        unsigned depth{};
        unsigned char savedSkip{};
        Publisher* publisher{};
    };
    static inline thread_local Local local_{};
    SRWLOCK lock_{SRWLOCK_INIT};
    Bindings bindings_{};
    bool selected_{};
    bool accepting_{};
    std::atomic_uint active_{};
    std::atomic_bool fallback_{};
    std::atomic_bool deferredFocus_{};

public:
    struct Token {
        Bindings bindings{};
        bool admitted{};
        bool scoped{};
    };

    bool configure(const Bindings& bindings) noexcept {
        AcquireSRWLockExclusive(&lock_);
        const bool idle = active_.load(std::memory_order_acquire) == 0;
        if (idle) {
            bindings_ = bindings;
            selected_ = accepting_ = true;
            fallback_.store(false, std::memory_order_release);
            deferredFocus_.store(false, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&lock_);
        return idle;
    }

    bool available() noexcept {
        AcquireSRWLockShared(&lock_);
        const bool result = selected_ && accepting_
            && !fallback_.load(std::memory_order_acquire);
        ReleaseSRWLockShared(&lock_);
        return result;
    }

    bool configured() noexcept {
        AcquireSRWLockShared(&lock_);
        const bool result = selected_ && accepting_;
        ReleaseSRWLockShared(&lock_);
        return result;
    }

    bool stop() noexcept {
        AcquireSRWLockExclusive(&lock_);
        accepting_ = false;
        const bool idle = active_.load(std::memory_order_acquire) == 0;
        if (idle) {
            selected_ = false;
            bindings_ = {};
        }
        ReleaseSRWLockExclusive(&lock_);
        return idle;
    }

    bool fallback_requested() const noexcept {
        return fallback_.load(std::memory_order_acquire);
    }

private:
    Token admit() noexcept {
        Token token{};
        active_.fetch_add(1, std::memory_order_acq_rel);
        AcquireSRWLockShared(&lock_);
        if (selected_ && accepting_) {
            token.bindings = bindings_;
            token.admitted = true;
        }
        ReleaseSRWLockShared(&lock_);
        if (!token.admitted) active_.fetch_sub(1, std::memory_order_acq_rel);
        return token;
    }

    static void release_reentrant_owner(const Bindings& bindings) noexcept {
        for (Publisher* publisher = local_.publisher; publisher; publisher = publisher->previous) {
            if (!publisher->queueOwned || *bindings.primary == 0) continue;
            if (bindings.modal()) bindings.setPrimary(false);
            bindings.queueLeave(bindings.queueId, 0);
            publisher->queueOwned = false;
        }
    }

    static void publish(const Bindings& bindings) noexcept {
        release_reentrant_owner(bindings);
        const bool desired = *bindings.windowFocused != 0
            && bindings.foregroundMatches(bindings.window);
        if (*bindings.changeSuppressed != 0 || (*bindings.focus != 0) == desired) {
            bindings.observeFocus();
            return;
        }
        Publisher publisher{};
        publisher.previous = local_.publisher;
        local_.publisher = &publisher;
        // loss may synchronously wait for graphics, so it must not own the input queue
        __try {
            if (desired) {
                bindings.queueEnter(bindings.queueId, 0);
                publisher.queueOwned = true;
            }
            bindings.publishFocus(bindings.object, bindings.window, desired);
        } __finally {
            if (publisher.queueOwned) {
                // a nested visibility close is real: do not re-pause input after it closed
                if (bindings.modal()) bindings.setPrimary(false);
                bindings.queueLeave(bindings.queueId, 0);
                publisher.queueOwned = false;
            }
            local_.publisher = publisher.previous;
        }
        bindings.observeFocus();
    }

public:
    Token begin(HWND window, UINT message, bool permitPublication = true) noexcept {
        // the native dispatcher excludes this whole command range from focus polling
        if (message >= WM_USER && message <= WM_USER + 15) return {};
        Token token = admit();
        bool complete = false;
        __try {
            begin_admitted(token, window, permitPublication);
            complete = true;
        } __finally {
            if (!complete) end(token);
        }
        return token;
    }

private:
    void begin_admitted(Token& token, HWND window, bool permitPublication) noexcept {
        if (!token.admitted || !token.bindings.modal()) return;
        const auto& bindings = token.bindings;
        const bool identity = window == bindings.window
            && GetCurrentThreadId() == bindings.windowThread;
        const auto skip = *bindings.skipFocus;
        if (!identity || skip > 1 || fallback_requested()) {
            fallback_.store(true, std::memory_order_release);
            bindings.requestRealGuard();
            if (bindings.realGuardAttached()) return;
            // native-window ownership can migrate after the old binding drains; a nonzero foreign
            // guard already defers the original focus head, so preserve it
            if (!bindings.currentWindowOwner(window) || skip > 1) return;
        }
        if (local_.depth == 0) {
            local_.policy = this;
            local_.savedSkip = skip;
        } else if (local_.policy != this) {
            fallback_.store(true, std::memory_order_release);
            bindings.requestRealGuard();
            return;
        }
        ++local_.depth;
        token.scoped = true;
        *bindings.skipFocus = 1;
        release_reentrant_owner(bindings);
        // respect a native Present's inherited focus deferral; only our own outer scopes publish
        if (local_.savedSkip == 0 && !fallback_requested()) {
            if (permitPublication) publish(bindings);
            else deferredFocus_.store(true, std::memory_order_release);
        }
        // a nested native 40D may clear its own byte before returning to this scope
        *bindings.skipFocus = 1;
    }

public:
    void end(Token& token) noexcept {
        if (!token.admitted) return;
        if (token.scoped) {
            --local_.depth;
            *token.bindings.skipFocus = local_.depth != 0 ? 1 : local_.savedSkip;
            if (local_.depth == 0) {
                local_.policy = nullptr;
                local_.savedSkip = 0;
            }
        }
        token.admitted = false;
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    bool prepare_open(void (*pause)()) noexcept {
        Token token = admit();
        if (!token.admitted) return false;
        __try {
            token.bindings.queueEnter(token.bindings.queueId, 0);
            __try { pause(); }
            __finally { token.bindings.queueLeave(token.bindings.queueId, 0); }
        } __finally { end(token); }
        return true;
    }

    void prepare_close(bool permitPublication = true) noexcept {
        Token token = admit();
        if (!token.admitted) return;
        __try {
            if (!fallback_requested() && permitPublication
                && GetCurrentThreadId() == token.bindings.windowThread
                && *token.bindings.skipFocus == 0) {
                const auto previous = *token.bindings.skipFocus;
                *token.bindings.skipFocus = 1;
                __try {
                    publish(token.bindings);
                    deferredFocus_.store(false, std::memory_order_release);
                }
                __finally { *token.bindings.skipFocus = local_.depth != 0 ? 1 : previous; }
            }
        } __finally { end(token); }
    }

    void service_deferred(bool permitPublication) noexcept {
        if (permitPublication && deferredFocus_.load(std::memory_order_acquire))
            prepare_close(true);
    }
};

} // namespace dawn::client::hooks::polled_input::native_key
