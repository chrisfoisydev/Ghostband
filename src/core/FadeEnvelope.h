// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include "FollowConstants.h"

#include <algorithm>
#include <atomic>
#include <cstddef>

namespace follow::core {

/// A linear gain ramp used for PANIC, mute, and degraded-mode fades.
///
/// **Why linear and not one-pole.** A one-pole fade never actually reaches zero — it
/// asymptotes. For PANIC that is unacceptable: "roughly 20–50 ms to silence" has to mean
/// *silence*, on time, every time, so the performer can trust the pedal. A linear ramp
/// reaches exactly 0.0 after exactly `fadeTimeMs` and then stays there. The audible
/// difference over 30 ms is negligible; the guarantee is not.
///
/// **Why not use MRT2's `set_mute()`.** It is smoothed, but with an internal, unspecified
/// time constant we cannot set, and it lives behind the inference thread — if that thread
/// is wedged, mute may never take effect. This envelope gates audio we have *already*
/// read into our own buffer, so PANIC works even when MRT2 is dead. That is the entire
/// point (see ARCHITECTURE.md §5).
///
/// Real-time safety: no allocation, no locks, no branches on unbounded data. `process()`
/// is one compare, one add, one multiply per sample.
class FadeEnvelope {
public:
    FadeEnvelope() = default;

    /// Configure. **Not** real-time safe conceptually (call from prepare/UI), though it
    /// only writes plain members and atomics.
    void prepare(double sampleRate, float fadeTimeMs) noexcept {
        sample_rate_ = sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate);
        setFadeTimeMs(fadeTimeMs);
    }

    void setFadeTimeMs(float ms) noexcept {
        fade_ms_ = std::clamp(ms, kMinPanicFadeMs, kMaxPanicFadeMs);
        const double samples = (static_cast<double>(fade_ms_) * 0.001) * sample_rate_;
        // Guard against a degenerate sample rate producing a zero-length ramp.
        full_ramp_samples_ = samples > 1.0 ? static_cast<int>(samples + 0.5) : 1;
    }

    float fadeTimeMs() const noexcept { return fade_ms_; }

    /// Number of samples a full 1->0 (or 0->1) ramp takes.
    std::size_t fadeLengthSamples() const noexcept {
        return static_cast<std::size_t>(full_ramp_samples_);
    }

    /// @name Control — safe from any thread (UI, MIDI, key handler)
    /// @{
    void fadeOut() noexcept { target_.store(0.0f, std::memory_order_relaxed); }
    void fadeIn() noexcept { target_.store(1.0f, std::memory_order_relaxed); }
    void setOpen(bool open) noexcept { open ? fadeIn() : fadeOut(); }
    /// @}

    /// Jump immediately, skipping the ramp. Only for initialisation and device changes —
    /// never as a response to a performer action, because it clicks.
    void snapTo(float gain) noexcept {
        const float g = std::clamp(gain, 0.0f, 1.0f);
        target_.store(g, std::memory_order_relaxed);
        current_ = g;
        last_target_ = g;
        ramp_start_ = g;
        ramp_pos_ = 0;
        ramp_len_ = 0;
        reported_.store(g, std::memory_order_relaxed);
    }

    bool isOpen() const noexcept { return target_.load(std::memory_order_relaxed) > 0.5f; }

    /// Last gain reached, for metering/tests. Lags by up to one block.
    float gain() const noexcept { return reported_.load(std::memory_order_relaxed); }

    /// True once a fade-out has fully reached silence.
    bool isSilent() const noexcept { return gain() <= 0.0f; }

    /// Advance one sample and return the gain to apply. Audio thread.
    ///
    /// Interpolates over an **integer** sample counter rather than accumulating a float
    /// step. That is what makes "silence after exactly `fadeTimeMs`" a guarantee: a
    /// float accumulator lands a few ULPs above zero after the nominal sample count and
    /// needs extra samples to clamp, which would make the PANIC timing approximate. The
    /// counter lands on the target exactly, on the sample we promised.
    float nextGain() noexcept {
        const float target = target_.load(std::memory_order_relaxed);

        if (target != last_target_) {
            // Re-aim from wherever we currently are. Ramp length is proportional to the
            // remaining distance, so the slew *rate* stays constant — a panic during an
            // in-flight fade does not suddenly change speed, and a short hop does not
            // take the full fade time.
            ramp_start_ = current_;
            last_target_ = target;
            const float distance = std::abs(target - ramp_start_);
            ramp_len_ = std::max(1, static_cast<int>(
                distance * static_cast<float>(full_ramp_samples_) + 0.5f));
            ramp_pos_ = 0;
        }

        if (ramp_pos_ >= ramp_len_) {
            current_ = last_target_;
            return current_;
        }

        ++ramp_pos_;
        if (ramp_pos_ >= ramp_len_) {
            current_ = last_target_; // exact landing, no residue
        } else {
            const float t = static_cast<float>(ramp_pos_) / static_cast<float>(ramp_len_);
            current_ = ramp_start_ + (last_target_ - ramp_start_) * t;
        }
        return current_;
    }

    /// Apply the ramp in place to an interleaved-by-channel stereo pair. Audio thread.
    void process(float* left, float* right, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            const float g = nextGain();
            left[i] *= g;
            right[i] *= g;
        }
        reported_.store(current_, std::memory_order_relaxed);
    }

private:
    double sample_rate_ = static_cast<double>(kSampleRate);
    float fade_ms_ = kDefaultPanicFadeMs;
    int full_ramp_samples_ = static_cast<int>(
        0.001 * static_cast<double>(kDefaultPanicFadeMs) * static_cast<double>(kSampleRate));

    std::atomic<float> target_{1.0f};
    std::atomic<float> reported_{1.0f};

    /// @name Audio thread only
    /// @{
    float current_ = 1.0f;
    float last_target_ = 1.0f;
    float ramp_start_ = 1.0f;
    int ramp_pos_ = 0;
    int ramp_len_ = 0;
    /// @}
};

} // namespace follow::core
