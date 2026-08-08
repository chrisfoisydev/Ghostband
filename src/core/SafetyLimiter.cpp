// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.

#include "SafetyLimiter.h"

#include <algorithm>
#include <cmath>

namespace follow::core {

namespace {
/// Lookahead == attack time. Long enough to reduce gain smoothly ahead of a transient,
/// short enough that the added latency on the AI bus is inconsequential.
constexpr float kLookaheadMs = 2.0f;
/// Slow enough to leave musical dynamics alone (the brief warns against squashing them),
/// fast enough that one loud stab does not duck the following bar.
constexpr float kReleaseMs = 120.0f;
constexpr float kMinDb = -120.0f;

/// One-pole coefficient for a given time constant.
float timeConstantCoeff(float ms, double sampleRate) noexcept {
    if (ms <= 0.0f) return 0.0f;
    const double samples = (static_cast<double>(ms) * 0.001) * sampleRate;
    if (samples < 1.0) return 0.0f;
    return static_cast<float>(std::exp(-1.0 / samples));
}
} // namespace

float linearToDb(float linear) noexcept {
    const float a = std::fabs(linear);
    if (!(a > 0.0f)) return kMinDb; // also catches NaN
    const float db = 20.0f * std::log10(a);
    return db < kMinDb ? kMinDb : db;
}

float dbToLinear(float db) noexcept {
    return std::pow(10.0f, db * 0.05f);
}

void SafetyLimiter::prepare(double sampleRate, std::size_t maxBlockSamples) {
    sample_rate_ = sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate);

    lookahead_ = static_cast<std::size_t>(
        (static_cast<double>(kLookaheadMs) * 0.001) * sample_rate_);
    lookahead_ = std::max<std::size_t>(lookahead_, 1);

    // Sized for lookahead plus a full block so process() never needs to grow it.
    const std::size_t capacity = lookahead_ + maxBlockSamples + 1;
    delay_l_.assign(capacity, 0.0f);
    delay_r_.assign(capacity, 0.0f);

    attack_coeff_ = timeConstantCoeff(kLookaheadMs, sample_rate_);
    release_coeff_ = timeConstantCoeff(kReleaseMs, sample_rate_);

    setCeilingDb(ceiling_db_.load(std::memory_order_relaxed));
    reset();
}

void SafetyLimiter::setCeilingDb(float db) noexcept {
    const float clamped = std::clamp(db, -24.0f, 0.0f);
    ceiling_db_.store(clamped, std::memory_order_relaxed);
    ceiling_lin_.store(dbToLinear(clamped), std::memory_order_relaxed);
}

void SafetyLimiter::reset() noexcept {
    std::fill(delay_l_.begin(), delay_l_.end(), 0.0f);
    std::fill(delay_r_.begin(), delay_r_.end(), 0.0f);
    write_pos_ = 0;
    envelope_ = 1.0f;
    out_peak_db_.store(kMinDb, std::memory_order_relaxed);
    gr_db_.store(0.0f, std::memory_order_relaxed);
}

void SafetyLimiter::pushAndPop(float inL, float inR, float& outL, float& outR) noexcept {
    const std::size_t capacity = delay_l_.size();
    const std::size_t read_pos = (write_pos_ + capacity - lookahead_) % capacity;

    outL = delay_l_[read_pos];
    outR = delay_r_[read_pos];

    delay_l_[write_pos_] = inL;
    delay_r_[write_pos_] = inR;

    write_pos_ = (write_pos_ + 1) % capacity;
}

void SafetyLimiter::process(float* left, float* right, std::size_t numSamples) noexcept {
    if (delay_l_.empty()) return; // prepare() not called; fail silent rather than crash

    const float ceiling = ceiling_lin_.load(std::memory_order_relaxed);
    const bool enabled = enabled_.load(std::memory_order_relaxed);

    float block_peak = 0.0f;
    float max_gr = 0.0f;

    for (std::size_t i = 0; i < numSamples; ++i) {
        float in_l = left[i];
        float in_r = right[i];

        // A generative model can emit NaN/Inf on a bad step. Scrubbing here means one
        // bad frame is a dropout, not a permanently poisoned signal path (NaN would
        // otherwise propagate through the envelope and mute us forever).
        if (!std::isfinite(in_l)) in_l = 0.0f;
        if (!std::isfinite(in_r)) in_r = 0.0f;

        // Detector runs on the *undelayed* input: that is what buys us lookahead.
        const float detect = std::max(std::fabs(in_l), std::fabs(in_r));

        float delayed_l, delayed_r;
        pushAndPop(in_l, in_r, delayed_l, delayed_r);

        if (enabled) {
            const float target = (detect > ceiling && detect > 0.0f) ? (ceiling / detect) : 1.0f;

            // Attack when clamping down, release when recovering. Asymmetry is what
            // makes a limiter transparent instead of pumping.
            const float coeff = (target < envelope_) ? attack_coeff_ : release_coeff_;
            envelope_ = target + coeff * (envelope_ - target);

            delayed_l *= envelope_;
            delayed_r *= envelope_;

            // Backstop. The smoother is good but not instantaneous; the ceiling is a
            // promise, so we enforce it unconditionally.
            delayed_l = std::clamp(delayed_l, -ceiling, ceiling);
            delayed_r = std::clamp(delayed_r, -ceiling, ceiling);

            max_gr = std::max(max_gr, 1.0f - envelope_);
        }

        left[i] = delayed_l;
        right[i] = delayed_r;
        block_peak = std::max(block_peak, std::max(std::fabs(delayed_l), std::fabs(delayed_r)));
    }

    out_peak_db_.store(linearToDb(block_peak), std::memory_order_relaxed);
    gr_db_.store(max_gr > 0.0f ? -linearToDb(1.0f - max_gr) : 0.0f, std::memory_order_relaxed);
}

SafetyLimiter::Status SafetyLimiter::status() const noexcept {
    if (!isEnabled()) return Status::Bypassed;
    // 0.1 dB threshold: below this the reduction is inaudible and flagging it would make
    // the stage indicator flicker for no reason.
    return gainReductionDb() > 0.1f ? Status::Limiting : Status::Safe;
}

} // namespace follow::core
