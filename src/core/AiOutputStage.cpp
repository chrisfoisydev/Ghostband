// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "AiOutputStage.h"

#include <algorithm>
#include <cmath>

namespace ghostband::core {

namespace {
/// Level-change smoothing. Fast enough to feel immediate on a fader, slow enough that a
/// jumped value does not zipper.
constexpr float kLevelSmoothingMs = 15.0f;
} // namespace

void AiOutputStage::prepare(double sampleRate, std::size_t maxBlockSamples) {
    const double sr = sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate);
    sample_rate_ = sr;

    fade_.prepare(sr, fade_.fadeTimeMs());
    limiter_.prepare(sr, maxBlockSamples);
    monitor_.prepare(sr);

    diagnostics_.setSampleRate(sr);

    const double smoothing_samples = (static_cast<double>(kLevelSmoothingMs) * 0.001) * sr;
    level_smoothing_coeff_ = smoothing_samples > 1.0
        ? static_cast<float>(std::exp(-1.0 / smoothing_samples))
        : 0.0f;

    smoothed_level_ = level_linear_.load(std::memory_order_relaxed);

    // Start closed. An output stage that comes up open would emit whatever is in the
    // backend's buffer the instant audio starts — including, potentially, a full-scale
    // partial frame. Opening is always an explicit act.
    fade_.snapTo(0.0f);
    updateFadeTarget();

    prepared_ = true;
}

void AiOutputStage::panic() noexcept {
    panicked_.store(true, std::memory_order_relaxed);
    updateFadeTarget();
}

void AiOutputStage::clearPanic() noexcept {
    panicked_.store(false, std::memory_order_relaxed);
    updateFadeTarget();
}

void AiOutputStage::setMuted(bool muted) noexcept {
    muted_.store(muted, std::memory_order_relaxed);
    updateFadeTarget();
}

void AiOutputStage::setOutputLevelDb(float db) noexcept {
    const float clamped = std::clamp(db, -60.0f, 12.0f);
    level_db_.store(clamped, std::memory_order_relaxed);
    // -60 dB is treated as -inf so a fader at the bottom is true silence, not a whisper.
    level_linear_.store(clamped <= -60.0f ? 0.0f : dbToLinear(clamped),
                        std::memory_order_relaxed);
}

void AiOutputStage::recoverFromDegraded() noexcept {
    monitor_.recover();
    updateFadeTarget();
}

void AiOutputStage::updateFadeTarget() noexcept {
    // Three independent reasons to be silent, any one of which wins. Recomputing the
    // target from all three (rather than toggling the envelope from each call site)
    // means clearing panic cannot accidentally un-mute, and recovering from Degraded
    // cannot accidentally cancel a panic.
    const bool open = !panicked_.load(std::memory_order_relaxed)
                   && !muted_.load(std::memory_order_relaxed)
                   && !monitor_.shouldMuteAi();
    fade_.setOpen(open);
}

void AiOutputStage::process(float* left, float* right, std::size_t numSamples,
                            bool underran) noexcept {
    if (!prepared_ || numSamples == 0) return;

    // 1. Fault accounting first, so a degraded verdict takes effect on *this* block
    //    rather than the next one.
    monitor_.reportBlock(numSamples, underran);
    diagnostics_.noteBlock(numSamples, underran);
    updateFadeTarget();

    // 2. Output level, smoothed per sample to avoid zipper noise on fader moves.
    const float target_level = level_linear_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < numSamples; ++i) {
        smoothed_level_ = target_level + level_smoothing_coeff_ * (smoothed_level_ - target_level);
        left[i] *= smoothed_level_;
        right[i] *= smoothed_level_;
    }

    // 3. Mute / panic / degraded fade. After the level so that a panic during a fader
    //    move still reaches exact silence in the configured time.
    fade_.process(left, right, numSamples);

    // 4. Safety limiter — last thing before the device, so nothing downstream of it can
    //    reintroduce a peak.
    limiter_.process(left, right, numSamples);

    // 5. Metering on the final signal, which is what the performer actually hears.
    float peak = 0.0f;
    double sum_squares = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float l = left[i];
        const float r = right[i];
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        const double dl = static_cast<double>(l);
        const double dr = static_cast<double>(r);
        sum_squares += dl * dl + dr * dr;
    }
    const float rms = static_cast<float>(std::sqrt(sum_squares / (2.0 * static_cast<double>(numSamples))));

    peak_db_.store(linearToDb(peak), std::memory_order_relaxed);
    rms_db_.store(linearToDb(rms), std::memory_order_relaxed);
}

} // namespace ghostband::core
