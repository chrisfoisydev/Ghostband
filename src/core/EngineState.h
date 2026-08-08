// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include <atomic>
#include <string>

namespace follow::core {

/// Lifecycle of the generation backend, as surfaced to the performer.
///
/// Deliberately coarse. The performer needs to know "can I rely on the band right now",
/// not which of eleven internal substates we are in. The UI maps these directly onto the
/// AI BAND status block from the brief (§6).
enum class EngineState {
    Unloaded, ///< No model. Nothing to fall back on; this is the honest cold state.
    Loading,  ///< Model load in flight. Can take seconds; never blocks audio.
    Ready,    ///< Model resident, generation stopped. Silent but instantly startable.
    Running,  ///< Generating. The only state in which audio should be non-silent.
    Error     ///< Load or generation failed. Requires explicit operator recovery.
};

const char* toString(EngineState s) noexcept;

/// Human-facing label for the status block ("READY", "GENERATING", ...).
const char* toDisplayString(EngineState s) noexcept;

/// Thread-safe state machine with an explicit legal-transition table.
///
/// Why a table rather than ad-hoc stores: a live app fails in ways nobody rehearsed, and
/// the dangerous failure is not "wrong state" but "state that says Running while nothing
/// generates". Rejecting illegal transitions turns that class of bug into a log line
/// instead of silence on stage.
///
/// All operations are lock-free. `state()` is safe from any thread including audio,
/// though the audio path should prefer `AiOutputStage`'s own atomics — it must not make
/// policy decisions, only apply gain.
class EngineStateMachine {
public:
    EngineStateMachine() = default;

    EngineState state() const noexcept { return state_.load(std::memory_order_acquire); }

    bool isRunning() const noexcept { return state() == EngineState::Running; }
    bool hasModel() const noexcept {
        const auto s = state();
        return s == EngineState::Ready || s == EngineState::Running;
    }

    /// True iff `from -> to` is a legal edge.
    static bool isLegalTransition(EngineState from, EngineState to) noexcept;

    /// Attempt a transition. Returns false (and changes nothing) if illegal, or if the
    /// current state is not `from` — the CAS makes concurrent callers safe.
    bool transition(EngineState from, EngineState to) noexcept;

    /// Transition from whatever the current state is, if legal. Returns false if not.
    bool transitionTo(EngineState to) noexcept;

    /// Force into Error with a reason. Always legal — failure can arrive at any moment,
    /// and refusing to record it would be the worst possible behaviour.
    void fail(std::string reason) noexcept;

    /// Reason recorded by the most recent `fail()`. Empty if never failed.
    /// Not audio-thread safe (returns a std::string); poll from the UI thread.
    std::string errorReason() const;

    /// Clear an error and return to Unloaded. The only exit from Error: recovery is
    /// explicit, never automatic, so the band cannot silently reappear mid-song.
    bool clearError() noexcept;

    /// Monotonic counter of transitions, for diagnostics and test assertions.
    std::uint64_t transitionCount() const noexcept {
        return transitions_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<EngineState> state_{EngineState::Unloaded};
    std::atomic<std::uint64_t> transitions_{0};

    mutable std::atomic_flag reason_lock_ = ATOMIC_FLAG_INIT;
    std::string error_reason_;

    void setReason(std::string r) const noexcept;
};

} // namespace follow::core
