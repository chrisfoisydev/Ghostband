// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include "FollowConstants.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace follow::core {

/// Health of the generated-audio path, as judged from underrun behaviour.
enum class Health {
    Healthy,  ///< No recent faults.
    Warning,  ///< Isolated underruns. Audible as a tick at worst; band keeps playing.
    Degraded  ///< Sustained underruns. AI is faded out and stays out until recovered.
};

const char* toString(Health h) noexcept;
const char* toDisplayString(Health h) noexcept;

/// Underrun policy for the AI bus.
///
/// **Why a policy object rather than an `if` in the callback.** The interesting question
/// is not "did a block underrun" — one underrun is a tick nobody notices. It is "is the
/// machine failing to keep up", which only a *rate over time* can answer. Putting that
/// judgement in one tested place stops it being re-invented, differently and wrongly, at
/// three call sites.
///
/// **Why latching.** Once we have faded the band out mid-song, an automatic un-mute the
/// moment the rate dips is worse than staying silent: the band reappearing unannounced,
/// possibly mid-phrase, is a bigger stage problem than its absence. Recovery is explicit
/// (`recover()`), driven by the performer or by an operator action. This implements the
/// brief's §5: fade, warn, allow restart — never uncontrolled noise.
///
/// Time is measured in **samples**, not wall clock, so `reportBlock()` needs no clock
/// call and behaves identically under test.
///
/// Real-time safety: `reportBlock()` is relaxed atomics only.
class SafetyMonitor {
public:
    SafetyMonitor() = default;

    void prepare(double sampleRate) noexcept {
        sample_rate_ = sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate);
        recomputeWindow();
        reset();
    }

    /// Underruns within one window that trip `Degraded`. Default 8 — roughly a fifth of
    /// a second of broken audio at a 128-sample block, which is unmistakably a fault
    /// rather than a blip.
    void setPolicy(int maxUnderrunsPerWindow, double windowSeconds) noexcept {
        max_per_window_.store(maxUnderrunsPerWindow > 0 ? maxUnderrunsPerWindow : 1,
                              std::memory_order_relaxed);
        window_seconds_ = windowSeconds > 0.0 ? windowSeconds : 2.0;
        recomputeWindow();
    }

    int maxUnderrunsPerWindow() const noexcept {
        return max_per_window_.load(std::memory_order_relaxed);
    }
    double windowSeconds() const noexcept { return window_seconds_; }

    /// Tell the monitor whether generation is supposed to be producing audio.
    ///
    /// **This is load-bearing.** When generation is stopped — or loaded but not yet
    /// started — the backend's ring buffer is legitimately empty and `readStereo()`
    /// returns false on every block. Those are not faults; they are the expected state.
    /// Counting them was a real bug: it tripped `Degraded` within ~2 s of loading a
    /// model and latched the AI to silence *before the performer ever pressed START*.
    ///
    /// Rising edge also arms a priming grace window, because even a healthy engine
    /// underruns for the first few frames while the ring buffer fills.
    ///
    /// Control thread.
    void setGenerating(bool generating) noexcept {
        const bool was = generating_.exchange(generating, std::memory_order_relaxed);
        if (generating && !was) {
            grace_remaining_.store(grace_length_samples_, std::memory_order_relaxed);
            window_underruns_.store(0, std::memory_order_relaxed);
            window_samples_elapsed_.store(0, std::memory_order_relaxed);
        }
    }
    bool isGenerating() const noexcept { return generating_.load(std::memory_order_relaxed); }

    /// How long after START underruns are tolerated while the ring buffer primes.
    /// Default 1 s — comfortably longer than the ~80 ms buffer plus model warm-up,
    /// short enough that a genuinely broken start is still caught quickly.
    void setPrimingGraceSeconds(double seconds) noexcept {
        grace_seconds_ = seconds > 0.0 ? seconds : 0.0;
        recomputeWindow();
    }

    /// True while the priming grace window is still open.
    bool isPriming() const noexcept {
        return grace_remaining_.load(std::memory_order_relaxed) > 0;
    }

    /// Report one processed audio block. Audio thread.
    void reportBlock(std::size_t numSamples, bool underran) noexcept {
        // Not generating: an empty buffer is expected, not a fault. Bail before any
        // accounting so a long pause at the Ready state cannot manufacture a failure.
        if (!generating_.load(std::memory_order_relaxed)) return;

        // Priming: the ring buffer has not filled yet. Underruns here are normal.
        const std::uint64_t grace = grace_remaining_.load(std::memory_order_relaxed);
        if (grace > 0) {
            grace_remaining_.store(numSamples >= grace ? 0 : grace - numSamples,
                                   std::memory_order_relaxed);
            if (underran) primed_underruns_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        reportBlockImpl(numSamples, underran);
    }

    /// Underruns absorbed by priming windows. Surfaced in diagnostics so a start that is
    /// quietly struggling is still visible rather than hidden by the grace period.
    std::uint64_t primingUnderruns() const noexcept {
        return primed_underruns_.load(std::memory_order_relaxed);
    }

    void reportBlockImpl(std::size_t numSamples, bool underran) noexcept {
        // Advance the window clock and roll it over if we have passed the boundary.
        const std::uint64_t elapsed =
            window_samples_elapsed_.fetch_add(numSamples, std::memory_order_relaxed) + numSamples;

        if (elapsed >= window_length_samples_) {
            window_samples_elapsed_.store(0, std::memory_order_relaxed);
            window_underruns_.store(0, std::memory_order_relaxed);
            // A clean window clears Warning, but never clears Degraded — that latches.
            Health expected = Health::Warning;
            health_.compare_exchange_strong(expected, Health::Healthy,
                                            std::memory_order_relaxed);
        }

        if (!underran) return;

        total_underruns_.fetch_add(1, std::memory_order_relaxed);
        const int count = window_underruns_.fetch_add(1, std::memory_order_relaxed) + 1;

        if (count >= max_per_window_.load(std::memory_order_relaxed)) {
            health_.store(Health::Degraded, std::memory_order_relaxed);
            degraded_events_.fetch_add(1, std::memory_order_relaxed);
        } else {
            Health expected = Health::Healthy;
            health_.compare_exchange_strong(expected, Health::Warning,
                                            std::memory_order_relaxed);
        }
    }

    Health health() const noexcept { return health_.load(std::memory_order_relaxed); }

    /// True when the AI bus must be silent. The output stage reads this every block.
    bool shouldMuteAi() const noexcept { return health() == Health::Degraded; }

    std::uint64_t totalUnderruns() const noexcept {
        return total_underruns_.load(std::memory_order_relaxed);
    }
    std::uint64_t degradedEvents() const noexcept {
        return degraded_events_.load(std::memory_order_relaxed);
    }

    /// Explicit operator recovery. Control thread only — never called automatically.
    ///
    /// Re-arms the priming grace: after a recovery the buffer may well be empty again,
    /// and immediately re-tripping on the refill would make recovery useless.
    void recover() noexcept {
        window_underruns_.store(0, std::memory_order_relaxed);
        window_samples_elapsed_.store(0, std::memory_order_relaxed);
        if (generating_.load(std::memory_order_relaxed)) {
            grace_remaining_.store(grace_length_samples_, std::memory_order_relaxed);
        }
        health_.store(Health::Healthy, std::memory_order_relaxed);
    }

    /// Full reset including history. Control thread.
    void reset() noexcept {
        recover();
        total_underruns_.store(0, std::memory_order_relaxed);
        degraded_events_.store(0, std::memory_order_relaxed);
        primed_underruns_.store(0, std::memory_order_relaxed);
    }

private:
    void recomputeWindow() noexcept {
        const double n = window_seconds_ * sample_rate_;
        window_length_samples_ = n > 1.0 ? static_cast<std::uint64_t>(n) : 1u;
        const double g = grace_seconds_ * sample_rate_;
        grace_length_samples_ = g > 0.0 ? static_cast<std::uint64_t>(g) : 0u;
    }

    std::atomic<bool> generating_{false};
    std::atomic<std::uint64_t> grace_remaining_{0};
    std::atomic<std::uint64_t> primed_underruns_{0};
    double grace_seconds_ = 1.0;
    std::uint64_t grace_length_samples_ = static_cast<std::uint64_t>(kSampleRate);

    double sample_rate_ = static_cast<double>(kSampleRate);
    double window_seconds_ = 2.0;
    std::uint64_t window_length_samples_ = static_cast<std::uint64_t>(2.0 * kSampleRate);

    std::atomic<int> max_per_window_{8};
    std::atomic<int> window_underruns_{0};
    std::atomic<std::uint64_t> window_samples_elapsed_{0};
    std::atomic<std::uint64_t> total_underruns_{0};
    std::atomic<std::uint64_t> degraded_events_{0};
    std::atomic<Health> health_{Health::Healthy};
};

} // namespace follow::core
