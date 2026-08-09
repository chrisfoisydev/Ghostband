// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#pragma once

#include "Diagnostics.h"
#include "FadeEnvelope.h"
#include "FollowConstants.h"
#include "SafetyLimiter.h"
#include "SafetyMonitor.h"

#include <atomic>
#include <cstddef>

namespace follow::core {

/// Everything that happens to generated audio between MRT2 and the audio device.
///
///   read from backend ──► [ mute/panic/degraded fade ] ──► [ output level ]
///                     ──► [ safety limiter ] ──► [ metering ] ──► device
///
/// This class is the reason PANIC is trustworthy. It owns the gain applied to audio we
/// have *already pulled* out of MRT2, so it cannot be blocked, starved, or outlived by a
/// wedged inference thread. It also has no dependency on MRT2 at all, which is what lets
/// it be tested off Apple Silicon.
///
/// Real-time safety: `process()` allocates nothing, locks nothing, and logs nothing.
/// All buffers are sized in `prepare()`.
class AiOutputStage {
public:
    AiOutputStage() = default;

    /// Allocate and reset. Call on start and on any device/sample-rate/block change.
    void prepare(double sampleRate, std::size_t maxBlockSamples = kMaxBlockSamples);

    /// Apply the full chain in place. Audio thread.
    ///
    /// @param underran  what the backend's `readStereo()` returned (false = underrun).
    ///                  Passed in rather than detected here so there is exactly one
    ///                  source of truth for "did generation keep up".
    void process(float* left, float* right, std::size_t numSamples, bool underran) noexcept;

    /// @name PANIC — callable from UI, keyboard, or MIDI thread
    /// @{
    /// Fade the AI to silence over the configured fade time (default 30 ms) and latch.
    void panic() noexcept;
    /// Release panic. Explicit, never automatic.
    void clearPanic() noexcept;
    bool isPanicked() const noexcept { return panicked_.load(std::memory_order_relaxed); }
    /// @}

    /// @name Mute — the "AI BAND ON/OFF" control (distinct from panic)
    ///
    /// Defaults to **muted**. Launching with the band already open would emit whatever
    /// happens to be in the backend's buffer the moment audio starts. "AI BAND OFF" is
    /// the correct cold state; turning it on is always a deliberate act.
    /// @{
    void setMuted(bool muted) noexcept;
    bool isMuted() const noexcept { return muted_.load(std::memory_order_relaxed); }
    /// @}

    /// Worst-case samples from `panic()` to true silence at the device.
    ///
    /// This is the fade **plus the limiter's lookahead delay** — audio already inside the
    /// delay line still has to drain, so the fade alone understates it. Reported honestly
    /// because "how fast does PANIC actually work" is a question a performer is entitled
    /// to a real answer to.
    std::size_t panicLatencySamples() const noexcept {
        return fade_.fadeLengthSamples() + limiter_.latencySamples();
    }
    float panicLatencyMs() const noexcept {
        const double sr = sample_rate_ > 0.0 ? sample_rate_ : static_cast<double>(kSampleRate);
        return static_cast<float>(1000.0 * static_cast<double>(panicLatencySamples()) / sr);
    }

    /// AI Output Level in dB — **how loud**, not how much the band plays.
    /// (AI Intensity is a separate macro over generation parameters; see
    /// ARCHITECTURE.md §6. Conflating them is explicitly forbidden by the brief.)
    void setOutputLevelDb(float db) noexcept;
    float outputLevelDb() const noexcept { return level_db_.load(std::memory_order_relaxed); }

    void setFadeTimeMs(float ms) noexcept { fade_.setFadeTimeMs(ms); }
    float fadeTimeMs() const noexcept { return fade_.fadeTimeMs(); }

    /// True once a fade-out has actually reached silence. Tests and the UI use this to
    /// confirm PANIC completed rather than merely being requested.
    bool isSilent() const noexcept { return fade_.isSilent(); }

    /// @name Metering — any thread, lags by up to one block
    /// @{
    float peakDb() const noexcept { return peak_db_.load(std::memory_order_relaxed); }
    float rmsDb() const noexcept { return rms_db_.load(std::memory_order_relaxed); }
    float gainReductionDb() const noexcept { return limiter_.gainReductionDb(); }
    SafetyLimiter::Status limiterStatus() const noexcept { return limiter_.status(); }
    /// @}

    SafetyMonitor& safetyMonitor() noexcept { return monitor_; }
    const SafetyMonitor& safetyMonitor() const noexcept { return monitor_; }
    SafetyLimiter& limiter() noexcept { return limiter_; }
    Diagnostics& diagnostics() noexcept { return diagnostics_; }
    const Diagnostics& diagnostics() const noexcept { return diagnostics_; }

    /// Recover from Degraded. Explicit operator action; re-opens the fade if nothing
    /// else (panic, mute) is holding it closed.
    void recoverFromDegraded() noexcept;

    /// Tell the safety stage whether generation is supposed to be producing audio.
    /// Must be called on every start/stop: when generation is stopped, an empty backend
    /// buffer is expected rather than a fault, and policing it as a fault will latch the
    /// AI to silence. See SafetyMonitor::setGenerating.
    void setGenerating(bool generating) noexcept { monitor_.setGenerating(generating); }

private:
    void updateFadeTarget() noexcept;

    FadeEnvelope fade_;
    SafetyLimiter limiter_;
    SafetyMonitor monitor_;
    Diagnostics diagnostics_;

    double sample_rate_ = static_cast<double>(kSampleRate);

    std::atomic<bool> panicked_{false};
    std::atomic<bool> muted_{true}; ///< AI BAND OFF is the safe cold state — see setMuted
    std::atomic<float> level_db_{0.0f};
    std::atomic<float> level_linear_{1.0f};

    float smoothed_level_ = 1.0f; ///< audio thread only; de-zippers level changes
    float level_smoothing_coeff_ = 0.0f;

    std::atomic<float> peak_db_{-120.0f};
    std::atomic<float> rms_db_{-120.0f};

    bool prepared_ = false;
};

} // namespace follow::core
