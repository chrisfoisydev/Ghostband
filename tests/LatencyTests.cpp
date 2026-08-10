// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

#include "TestMain.h"
#include "core/ControlLatency.h"
#include "core/GhostBandConstants.h"

using namespace ghostband::core;

TEST_MAIN_BEGIN("ControlLatency")

TEST("the default configuration adds up to the documented figure") {
    // 80 ms generation buffer + 512-sample device block + 2 ms limiter lookahead,
    // plus half a 40 ms frame on average.
    const auto e = estimateControlLatency(48000.0, kDefaultGenerationBufferSamples, 512, 96);

    CHECK_NEAR(e.frameQuantisationMaxMs, 40.0f, 0.01);
    CHECK_NEAR(e.generationBufferMs, 80.0f, 0.01);
    CHECK_NEAR(e.deviceBufferMs, 10.67f, 0.01);
    CHECK_NEAR(e.limiterLookaheadMs, 2.0f, 0.01);

    CHECK_NEAR(e.typicalMs, 112.67f, 0.05);
    CHECK_NEAR(e.worstCaseMs, 132.67f, 0.05);
}

TEST("shrinking the generation buffer is the lever that actually moves the number") {
    const auto two_frames = estimateControlLatency(48000.0, 2 * kFrameSamples, 512, 96);
    const auto one_frame = estimateControlLatency(48000.0, kFrameSamples, 512, 96);

    // Exactly one frame period saved — the point of exposing the buffer as a control.
    CHECK_NEAR(two_frames.typicalMs - one_frame.typicalMs, 40.0f, 0.01);
}

TEST("a smaller device block helps, but far less") {
    const auto big = estimateControlLatency(48000.0, kDefaultGenerationBufferSamples, 512, 96);
    const auto small = estimateControlLatency(48000.0, kDefaultGenerationBufferSamples, 128, 96);
    const float saved = big.typicalMs - small.typicalMs;

    CHECK(saved > 7.0f);
    CHECK(saved < 9.0f);   // ~8 ms: real, but a quarter of what the generation buffer gives
}

TEST("worst case is exactly one frame worse than typical") {
    const auto e = estimateControlLatency(48000.0, 3840, 512, 96);
    CHECK_NEAR(e.worstCaseMs - e.typicalMs, e.frameQuantisationMaxMs * 0.5f, 0.01);
}

TEST("the model's own response time is reported separately, never folded in") {
    // Transport is ours and tunable; the model's ~200 ms is not. Folding them together
    // would make the total look immovable when most of it is not.
    const auto e = estimateControlLatency(48000.0, kDefaultGenerationBufferSamples, 512, 96);
    CHECK(e.typicalMs < 150.0f);
    CHECK_NEAR(e.plusModelResponseMs(200.0f) - e.typicalMs, 200.0f, 0.01);
}

TEST("a degenerate sample rate cannot produce nonsense") {
    const auto e = estimateControlLatency(0.0, 3840, 512, 96);
    CHECK(e.typicalMs > 0.0f);
    CHECK(e.generationBufferMs > 0.0f);  // falls back to 48 kHz rather than dividing by zero
}

TEST_MAIN_END()
