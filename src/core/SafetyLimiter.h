// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include "FollowConstants.h"

#include <atomic>
#include <cstddef>
#include <vector>

namespace follow::core {

/// Output safety stage for generated audio.
///
/// **Why this exists.** A generative model has no notion of headroom. A prompt change, a
/// degenerate sampling step, or a model error can produce a burst tens of dB above the
/// musical level. Through a PA at gig volume that is a genuine hazard to equipment and
/// ears, not merely an artefact. This stage guarantees the output never exceeds a
/// configured ceiling.
///
/// **Design.** Feed-forward peak limiter with a short lookahead delay (default 2 ms) so
/// gain reduction is fully applied *before* a transient arrives, rather than clipping it
/// and then reacting. Attack is matched to the lookahead; release is slow enough
/// (default 120 ms) that normal musical dynamics pass untouched — the brief explicitly
/// warns against processing that flattens the band. A final hard clamp backstops the
/// smoother: the ceiling is a guarantee, not a target.
///
/// The 2 ms of latency is deliberate and accounted for: it applies only to the AI stereo
/// bus, which is not time-critical against the performer's own acoustic sound in the way
/// a monitor path would be.
///
/// Real-time safety: all buffers are allocated in `prepare()`. `process()` performs no
/// allocation, no locks, and no branches on unbounded data.
class SafetyLimiter {
public:
    enum class Status {
        Safe,     ///< No gain reduction in the recent past.
        Limiting, ///< Actively reducing gain. Normal on peaks; constant = level too hot.
        Bypassed
    };

    SafetyLimiter() = default;

    /// Allocate and reset. Call before `process()` and on any device/blocksize change.
    void prepare(double sampleRate, std::size_t maxBlockSamples = kMaxBlockSamples);

    void setCeilingDb(float db) noexcept;
    float ceilingDb() const noexcept { return ceiling_db_.load(std::memory_order_relaxed); }

    void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    /// Limit in place. Audio thread.
    void process(float* left, float* right, std::size_t numSamples) noexcept;

    /// Clear internal state (delay line, envelope). Not audio-thread safe.
    void reset() noexcept;

    /// @name Metering — safe from any thread, lags by up to one block
    /// @{
    /// Peak level of the signal leaving the limiter, in dBFS. -inf reported as -120.
    float outputPeakDb() const noexcept { return out_peak_db_.load(std::memory_order_relaxed); }
    /// Current gain reduction in dB (>= 0; 0 means not limiting).
    float gainReductionDb() const noexcept { return gr_db_.load(std::memory_order_relaxed); }
    Status status() const noexcept;
    /// @}

    /// Added latency in samples (the lookahead). Report this to the host/user honestly.
    std::size_t latencySamples() const noexcept { return lookahead_; }

private:
    void pushAndPop(float inL, float inR, float& outL, float& outR) noexcept;

    double sample_rate_ = static_cast<double>(kSampleRate);
    std::size_t lookahead_ = 0;
    std::size_t write_pos_ = 0;

    std::vector<float> delay_l_;
    std::vector<float> delay_r_;

    float envelope_ = 1.0f;     ///< current gain multiplier, audio thread only
    float attack_coeff_ = 0.0f;
    float release_coeff_ = 0.0f;

    std::atomic<float> ceiling_db_{kDefaultLimiterCeilingDb};
    std::atomic<float> ceiling_lin_{0.0f};
    std::atomic<bool> enabled_{true};

    std::atomic<float> out_peak_db_{-120.0f};
    std::atomic<float> gr_db_{0.0f};
};

/// Linear amplitude -> dBFS, floored at -120 dB so meters and logs never carry -inf.
float linearToDb(float linear) noexcept;
/// dBFS -> linear amplitude.
float dbToLinear(float db) noexcept;

} // namespace follow::core
