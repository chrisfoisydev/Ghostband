// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "ControlLatency.h"

#include "GhostBandConstants.h"

namespace ghostband::core {

namespace {
float samplesToMs(std::size_t samples, double sampleRate) noexcept {
    if (sampleRate <= 0.0) return 0.0f;
    return static_cast<float>(1000.0 * static_cast<double>(samples) / sampleRate);
}
} // namespace

ControlLatencyEstimate estimateControlLatency(double sampleRate,
                                              std::size_t generationBufferSamples,
                                              std::size_t deviceBlockSamples,
                                              std::size_t limiterLookaheadSamples) noexcept {
    const double sr = sampleRate > 0.0 ? sampleRate : static_cast<double>(kSampleRate);

    ControlLatencyEstimate e;
    e.frameQuantisationMaxMs = samplesToMs(kFrameSamples, sr);
    e.generationBufferMs = samplesToMs(generationBufferSamples, sr);
    e.deviceBufferMs = samplesToMs(deviceBlockSamples, sr);
    e.limiterLookaheadMs = samplesToMs(limiterLookaheadSamples, sr);

    const float fixed = e.generationBufferMs + e.deviceBufferMs + e.limiterLookaheadMs;

    // Quantisation is uniform over [0, one frame): a control change is equally likely to
    // land anywhere within the frame the inference loop is already working on.
    e.typicalMs = fixed + e.frameQuantisationMaxMs * 0.5f;
    e.worstCaseMs = fixed + e.frameQuantisationMaxMs;
    return e;
}

} // namespace ghostband::core
