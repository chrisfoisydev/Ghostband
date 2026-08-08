// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#include "EngineState.h"

namespace follow::core {

const char* toString(EngineState s) noexcept {
    switch (s) {
        case EngineState::Unloaded: return "Unloaded";
        case EngineState::Loading:  return "Loading";
        case EngineState::Ready:    return "Ready";
        case EngineState::Running:  return "Running";
        case EngineState::Error:    return "Error";
    }
    return "Unknown";
}

const char* toDisplayString(EngineState s) noexcept {
    switch (s) {
        case EngineState::Unloaded: return "NO MODEL";
        case EngineState::Loading:  return "LOADING";
        case EngineState::Ready:    return "READY";
        case EngineState::Running:  return "GENERATING";
        case EngineState::Error:    return "ERROR";
    }
    return "UNKNOWN";
}

bool EngineStateMachine::isLegalTransition(EngineState from, EngineState to) noexcept {
    if (from == to) return false; // no-op transitions are a caller bug, not a transition

    // Failure can strike from anywhere.
    if (to == EngineState::Error) return true;

    switch (from) {
        case EngineState::Unloaded:
            return to == EngineState::Loading;

        case EngineState::Loading:
            return to == EngineState::Ready || to == EngineState::Unloaded;

        case EngineState::Ready:
            return to == EngineState::Running || to == EngineState::Unloaded;

        case EngineState::Running:
            // Must stop before unloading: tearing a model out from under a live
            // inference thread is exactly the crash we cannot have on stage.
            return to == EngineState::Ready;

        case EngineState::Error:
            // Recovery is explicit and goes through Unloaded, so a retry always
            // re-establishes the model from a known-cold state.
            return to == EngineState::Unloaded;
    }
    return false;
}

bool EngineStateMachine::transition(EngineState from, EngineState to) noexcept {
    if (!isLegalTransition(from, to)) return false;

    EngineState expected = from;
    if (!state_.compare_exchange_strong(expected, to,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        return false;
    }
    transitions_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool EngineStateMachine::transitionTo(EngineState to) noexcept {
    // Retry loop: another thread may transition between our load and CAS. Bounded by
    // the CAS succeeding or the edge becoming illegal, so it always terminates.
    for (;;) {
        const EngineState from = state_.load(std::memory_order_acquire);
        if (!isLegalTransition(from, to)) return false;

        EngineState expected = from;
        if (state_.compare_exchange_weak(expected, to,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            transitions_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
}

void EngineStateMachine::fail(std::string reason) noexcept {
    setReason(std::move(reason));
    const EngineState prev = state_.exchange(EngineState::Error, std::memory_order_acq_rel);
    if (prev != EngineState::Error) {
        transitions_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool EngineStateMachine::clearError() noexcept {
    if (!transition(EngineState::Error, EngineState::Unloaded)) return false;
    setReason({});
    return true;
}

std::string EngineStateMachine::errorReason() const {
    while (reason_lock_.test_and_set(std::memory_order_acquire)) { /* spin */ }
    std::string copy = error_reason_;
    reason_lock_.clear(std::memory_order_release);
    return copy;
}

void EngineStateMachine::setReason(std::string r) const noexcept {
    // A spinlock, not a mutex: the critical section is a string move, contention is
    // effectively zero (failures are rare), and this keeps the header free of <mutex>.
    // Never called from the audio thread — errorReason() is UI-thread polling only.
    while (reason_lock_.test_and_set(std::memory_order_acquire)) { /* spin */ }
    const_cast<std::string&>(error_reason_) = std::move(r);
    reason_lock_.clear(std::memory_order_release);
}

} // namespace follow::core
