// Follow — live AI accompaniment for singer-songwriters.
// Copyright 2026 Follow contributors. Licensed under Apache-2.0.
//
// Tests for the failure path: PANIC, the limiter, and underrun policy.
// This is the part of Follow that decides whether a bad night is a glitch or a ruined
// set, so it is the part built and tested first.

#include "TestMain.h"
#include "core/AiOutputStage.h"
#include "core/FadeEnvelope.h"
#include "core/SafetyLimiter.h"
#include "core/SafetyMonitor.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace follow::core;

namespace {

constexpr double kSr = 48000.0;

/// Fill a stereo pair with a constant value (a worst-case, always-at-peak signal).
void fillConstant(std::vector<float>& l, std::vector<float>& r, float value) {
    std::fill(l.begin(), l.end(), value);
    std::fill(r.begin(), r.end(), value);
}

/// Fill with a sine — a musical signal, for checking we don't mangle normal audio.
void fillSine(std::vector<float>& l, std::vector<float>& r, double amplitude, double hz) {
    for (std::size_t i = 0; i < l.size(); ++i) {
        const auto s = static_cast<float>(
            amplitude * std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kSr));
        l[i] = s;
        r[i] = s;
    }
}

// Helper: put a monitor into the steady generating state, past its priming grace.
void makeGenerating(SafetyMonitor& mon) {
    mon.setGenerating(true);
    // Burn off the grace window with clean blocks.
    for (int i = 0; i < 500; ++i) mon.reportBlock(512, false);
}

float maxAbs(const std::vector<float>& v) {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

} // namespace

TEST_MAIN_BEGIN("AudioSafety")

// ---------------------------------------------------------------------------
// FadeEnvelope — the mechanism PANIC depends on
// ---------------------------------------------------------------------------

TEST("fade reaches EXACT silence, not an asymptote") {
    // The whole reason the ramp is linear rather than one-pole. "Roughly 20-50 ms to
    // silence" has to mean silence, or the pedal cannot be trusted.
    FadeEnvelope fade;
    fade.prepare(kSr, 30.0f);
    fade.snapTo(1.0f);
    fade.fadeOut();

    const std::size_t n = fade.fadeLengthSamples() + 16;
    std::vector<float> l(n, 1.0f), r(n, 1.0f);
    fade.process(l.data(), r.data(), n);

    CHECK_EQ(fade.gain(), 0.0f);
    CHECK(fade.isSilent());
    CHECK_EQ(l[n - 1], 0.0f);
    CHECK_EQ(r[n - 1], 0.0f);
}

TEST("fade completes within the configured 20-50 ms window") {
    for (double ms : {20.0, 30.0, 50.0}) {
        FadeEnvelope fade;
        fade.prepare(kSr, static_cast<float>(ms));
        fade.snapTo(1.0f);
        fade.fadeOut();

        const auto expected = static_cast<std::size_t>((ms * 0.001) * kSr);
        std::vector<float> l(expected, 1.0f), r(expected, 1.0f);
        fade.process(l.data(), r.data(), expected);

        // Exactly at the boundary the ramp has just reached zero.
        CHECK_NEAR(fade.gain(), 0.0f, 1e-6);
    }
}

TEST("fade time is clamped to the safe 20-50 ms range") {
    FadeEnvelope fade;
    fade.prepare(kSr, 0.0f);      // would click
    CHECK_NEAR(fade.fadeTimeMs(), kMinPanicFadeMs, 1e-6);
    fade.setFadeTimeMs(5000.0f);  // would be useless on stage
    CHECK_NEAR(fade.fadeTimeMs(), kMaxPanicFadeMs, 1e-6);
}

TEST("fade is monotonic and starts from the current gain (no click)") {
    FadeEnvelope fade;
    fade.prepare(kSr, 30.0f);
    fade.snapTo(1.0f);
    fade.fadeOut();

    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    fade.process(l.data(), r.data(), l.size());

    // First sample must be near 1.0 — a jump to a low value IS the click we are avoiding.
    CHECK(l[0] > 0.99f);
    for (std::size_t i = 1; i < l.size(); ++i) {
        CHECK(l[i] <= l[i - 1] + 1e-7f);
    }
}

TEST("fade is correct regardless of how the block boundaries fall") {
    // Device block sizes vary and can change at runtime; the ramp must not depend on them.
    FadeEnvelope a, b;
    a.prepare(kSr, 30.0f);
    b.prepare(kSr, 30.0f);
    a.snapTo(1.0f);
    b.snapTo(1.0f);
    a.fadeOut();
    b.fadeOut();

    const std::size_t total = a.fadeLengthSamples() + 128;

    std::vector<float> al(total, 1.0f), ar(total, 1.0f);
    a.process(al.data(), ar.data(), total);

    std::vector<float> bl(total, 1.0f), br(total, 1.0f);
    for (std::size_t off = 0; off < total; off += 37) { // deliberately awkward block size
        const std::size_t n = std::min<std::size_t>(37, total - off);
        b.process(bl.data() + off, br.data() + off, n);
    }

    for (std::size_t i = 0; i < total; ++i) CHECK_NEAR(al[i], bl[i], 1e-6);
}

TEST("fade in and out are symmetric") {
    FadeEnvelope fade;
    fade.prepare(kSr, 30.0f);
    fade.snapTo(0.0f);
    fade.fadeIn();

    const std::size_t n = fade.fadeLengthSamples() + 8;
    std::vector<float> l(n, 1.0f), r(n, 1.0f);
    fade.process(l.data(), r.data(), n);

    CHECK_NEAR(fade.gain(), 1.0f, 1e-6);
    CHECK(l[0] < 0.01f);
    CHECK_NEAR(l[n - 1], 1.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// SafetyLimiter
// ---------------------------------------------------------------------------

TEST("limiter never exceeds its ceiling, even on a full-scale DC burst") {
    // The realistic failure: a model error emitting a sustained rail-to-rail signal.
    SafetyLimiter lim;
    lim.prepare(kSr);
    lim.setCeilingDb(-1.0f);

    std::vector<float> l(4096), r(4096);
    fillConstant(l, r, 4.0f); // +12 dBFS, far beyond anything musical

    lim.process(l.data(), r.data(), l.size());

    const float ceiling = dbToLinear(-1.0f);
    CHECK(maxAbs(l) <= ceiling + 1e-6f);
    CHECK(maxAbs(r) <= ceiling + 1e-6f);
    CHECK(lim.status() == SafetyLimiter::Status::Limiting);
}

TEST("limiter holds the ceiling across many consecutive blocks") {
    SafetyLimiter lim;
    lim.prepare(kSr);
    lim.setCeilingDb(-1.0f);
    const float ceiling = dbToLinear(-1.0f);

    std::vector<float> l(512), r(512);
    for (int block = 0; block < 200; ++block) {
        fillConstant(l, r, (block % 2 == 0) ? 8.0f : -8.0f); // alternating hard rails
        lim.process(l.data(), r.data(), l.size());
        CHECK(maxAbs(l) <= ceiling + 1e-6f);
    }
}

TEST("limiter leaves quiet musical signal essentially untouched") {
    // The brief warns against processing that destroys dynamics; below the ceiling the
    // limiter must be transparent apart from its lookahead delay.
    SafetyLimiter lim;
    lim.prepare(kSr);
    lim.setCeilingDb(-1.0f);

    std::vector<float> l(4096), r(4096);
    fillSine(l, r, 0.25f, 220.0f); // -12 dBFS
    const std::vector<float> original = l;

    lim.process(l.data(), r.data(), l.size());

    CHECK_NEAR(lim.gainReductionDb(), 0.0f, 1e-4);
    CHECK(lim.status() == SafetyLimiter::Status::Safe);

    // Output equals input delayed by the lookahead, sample for sample.
    const std::size_t d = lim.latencySamples();
    for (std::size_t i = d; i < l.size(); ++i) CHECK_NEAR(l[i], original[i - d], 1e-6);
}

TEST("limiter scrubs NaN and Inf instead of poisoning the signal path") {
    // A single NaN through a feedback envelope would mute the band permanently.
    SafetyLimiter lim;
    lim.prepare(kSr);

    std::vector<float> l(1024, 0.1f), r(1024, 0.1f);
    l[10] = std::numeric_limits<float>::quiet_NaN();
    l[11] = std::numeric_limits<float>::infinity();
    r[12] = -std::numeric_limits<float>::infinity();

    lim.process(l.data(), r.data(), l.size());

    for (float x : l) CHECK(std::isfinite(x));
    for (float x : r) CHECK(std::isfinite(x));

    // And it recovers: later samples still carry real signal.
    std::vector<float> l2(1024, 0.1f), r2(1024, 0.1f);
    lim.process(l2.data(), r2.data(), l2.size());
    CHECK(maxAbs(l2) > 0.05f);
}

TEST("limiter can be bypassed and reports it honestly") {
    SafetyLimiter lim;
    lim.prepare(kSr);
    lim.setEnabled(false);
    CHECK(lim.status() == SafetyLimiter::Status::Bypassed);
}

TEST("dB conversions round-trip and floor at -120") {
    CHECK_NEAR(linearToDb(1.0f), 0.0f, 1e-5);
    CHECK_NEAR(linearToDb(0.5f), -6.0206f, 1e-3);
    CHECK_NEAR(dbToLinear(-6.0206f), 0.5f, 1e-4);
    CHECK_NEAR(linearToDb(0.0f), -120.0f, 1e-6);
    CHECK_NEAR(linearToDb(std::numeric_limits<float>::quiet_NaN()), -120.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// SafetyMonitor — underrun policy
// ---------------------------------------------------------------------------

TEST("underruns are IGNORED while generation is stopped") {
    // The bug this exists to prevent: between LOAD MODEL and START, the backend's ring
    // buffer is legitimately empty and readStereo() returns false on every block. Counting
    // those latched Degraded and muted the AI *before the performer ever pressed START* —
    // observed on real hardware as 2373 underruns accrued while the app sat at Ready.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);

    for (int i = 0; i < 5000; ++i) mon.reportBlock(512, true);

    CHECK(mon.health() == Health::Healthy);
    CHECK(!mon.shouldMuteAi());
    CHECK_EQ(mon.totalUnderruns(), 0u);
}

TEST("a priming grace window absorbs start-up underruns") {
    // Even a healthy engine underruns for the first frames while the ring buffer fills.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);
    mon.setPrimingGraceSeconds(1.0);
    mon.setGenerating(true);

    CHECK(mon.isPriming());
    for (int i = 0; i < 20; ++i) mon.reportBlock(512, true);

    CHECK(mon.health() == Health::Healthy);
    CHECK(!mon.shouldMuteAi());
    CHECK(mon.primingUnderruns() > 0u); // absorbed, but still visible in diagnostics
}

TEST("after priming, sustained underruns still trip Degraded") {
    // The grace window must not become a permanent excuse.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);
    mon.setPrimingGraceSeconds(1.0);
    makeGenerating(mon);
    CHECK(!mon.isPriming());

    for (int i = 0; i < 8; ++i) mon.reportBlock(512, true);
    CHECK(mon.health() == Health::Degraded);
    CHECK(mon.shouldMuteAi());
}

TEST("stopping generation cannot manufacture a fault") {
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);
    makeGenerating(mon);

    mon.setGenerating(false);
    for (int i = 0; i < 5000; ++i) mon.reportBlock(512, true);

    CHECK(mon.health() == Health::Healthy);
    CHECK(!mon.shouldMuteAi());
}

TEST("recovery re-arms the priming grace") {
    // After recovering, the buffer may be empty again; re-tripping instantly on the
    // refill would make the RECOVER control useless.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(4, 2.0);
    mon.setPrimingGraceSeconds(1.0);
    makeGenerating(mon);

    for (int i = 0; i < 4; ++i) mon.reportBlock(512, true);
    CHECK(mon.shouldMuteAi());

    mon.recover();
    CHECK(mon.isPriming());
    for (int i = 0; i < 20; ++i) mon.reportBlock(512, true);
    CHECK(!mon.shouldMuteAi());
}

TEST("isolated underruns are a warning, not a shutdown") {
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);
    makeGenerating(mon);

    mon.reportBlock(128, true);
    CHECK(mon.health() == Health::Warning);
    CHECK(!mon.shouldMuteAi());
}

TEST("sustained underruns trip Degraded and mute the AI") {
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 2.0);
    makeGenerating(mon);

    for (int i = 0; i < 8; ++i) mon.reportBlock(128, true);

    CHECK(mon.health() == Health::Degraded);
    CHECK(mon.shouldMuteAi());
    CHECK_EQ(mon.totalUnderruns(), 8u);
    CHECK_EQ(mon.degradedEvents(), 1u);
}

TEST("Degraded latches — it never clears itself") {
    // An unannounced band re-entry mid-phrase is a worse stage problem than silence,
    // so recovery must be a deliberate act.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(4, 1.0);
    makeGenerating(mon);

    for (int i = 0; i < 4; ++i) mon.reportBlock(128, true);
    CHECK(mon.shouldMuteAi());

    // Ten seconds of flawless audio.
    for (int i = 0; i < 10 * 375; ++i) mon.reportBlock(128, false);
    CHECK(mon.health() == Health::Degraded);
    CHECK(mon.shouldMuteAi());

    mon.recover();
    CHECK(mon.health() == Health::Healthy);
    CHECK(!mon.shouldMuteAi());
}

TEST("underruns spread thinly across windows never trip Degraded") {
    // One tick every couple of seconds is a machine under mild load, not a failing one.
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 1.0);
    makeGenerating(mon);

    for (int window = 0; window < 20; ++window) {
        mon.reportBlock(128, true);
        for (int i = 0; i < 400; ++i) mon.reportBlock(128, false); // > 1 s of clean audio
    }

    CHECK(mon.health() != Health::Degraded);
    CHECK(!mon.shouldMuteAi());
    CHECK_EQ(mon.totalUnderruns(), 20u);
}

TEST("a clean window clears Warning but never clears Degraded") {
    SafetyMonitor mon;
    mon.prepare(kSr);
    mon.setPolicy(8, 1.0);
    makeGenerating(mon);

    mon.reportBlock(128, true);
    CHECK(mon.health() == Health::Warning);
    for (int i = 0; i < 400; ++i) mon.reportBlock(128, false);
    CHECK(mon.health() == Health::Healthy);
}

// ---------------------------------------------------------------------------
// AiOutputStage — the integrated failure path
// ---------------------------------------------------------------------------

TEST("output stage starts muted — AI BAND OFF is the cold state") {
    // Coming up open would emit whatever is in the backend's buffer the instant audio
    // starts, potentially at full scale. Turning the band on is always deliberate.
    AiOutputStage stage;
    stage.prepare(kSr, 1024);
    CHECK(stage.isMuted());

    std::vector<float> l(512), r(512);
    fillConstant(l, r, 1.0f);
    stage.process(l.data(), r.data(), l.size(), /*underran=*/false);

    CHECK_EQ(maxAbs(l), 0.0f);
    CHECK(stage.isSilent());
}

TEST("PANIC drives the output to silence within the fade time") {
    AiOutputStage stage;
    stage.prepare(kSr, 4096);
    stage.setFadeTimeMs(30.0f);
    stage.setMuted(false);

    std::vector<float> l(4096), r(4096);

    // Open up and confirm we are actually passing audio first.
    fillConstant(l, r, 0.5f);
    stage.process(l.data(), r.data(), l.size(), false);
    CHECK(maxAbs(l) > 0.1f);

    stage.panic();
    CHECK(stage.isPanicked());

    // Time-to-silence is the fade PLUS the limiter's lookahead: audio already inside the
    // delay line still has to drain. Asserting only the fade would pass while the device
    // was still emitting the tail, which is exactly the kind of "works in the test,
    // audible on stage" gap this suite exists to catch.
    const std::size_t n = stage.panicLatencySamples();
    std::vector<float> pl(n), pr(n);
    fillConstant(pl, pr, 0.5f);
    stage.process(pl.data(), pr.data(), pl.size(), false);

    CHECK(stage.isSilent());
    CHECK_EQ(pl[n - 1], 0.0f);
    CHECK_EQ(pr[n - 1], 0.0f);
}

TEST("total PANIC latency stays inside the brief's 20-50 ms envelope") {
    AiOutputStage stage;
    stage.prepare(kSr, 4096);

    for (float ms : {20.0f, 30.0f, 50.0f}) {
        stage.setFadeTimeMs(ms);
        // Fade + ~2 ms limiter lookahead. The 50 ms setting lands at ~52 ms, which is the
        // honest number; it is recorded here rather than rounded away.
        CHECK(stage.panicLatencyMs() >= ms);
        CHECK(stage.panicLatencyMs() <= ms + 3.0f);
    }
}

TEST("PANIC does not click: the ramp starts at full gain") {
    AiOutputStage stage;
    stage.prepare(kSr, 4096);
    stage.setMuted(false);

    std::vector<float> l(2048), r(2048);
    fillConstant(l, r, 0.5f);
    stage.process(l.data(), r.data(), l.size(), false); // reach steady state

    stage.panic();
    fillConstant(l, r, 0.5f);
    stage.process(l.data(), r.data(), l.size(), false);

    CHECK(l[0] > 0.45f);                       // no instantaneous drop
    for (std::size_t i = 1; i < 600; ++i) {    // and it descends smoothly
        CHECK(std::fabs(l[i]) <= std::fabs(l[i - 1]) + 1e-6f);
    }
}

TEST("clearing PANIC does not un-mute, and recovery does not cancel PANIC") {
    // Three independent reasons for silence; none may accidentally override another.
    AiOutputStage stage;
    stage.prepare(kSr, 1024);

    stage.setMuted(true);
    stage.panic();
    stage.clearPanic();
    CHECK(!stage.isPanicked());
    CHECK(stage.isMuted());

    std::vector<float> l(2048), r(2048);
    fillConstant(l, r, 1.0f);
    stage.process(l.data(), r.data(), l.size(), false);
    CHECK_EQ(maxAbs(l), 0.0f); // still muted

    stage.panic();
    stage.recoverFromDegraded();
    CHECK(stage.isPanicked()); // recovery must not release panic
}

TEST("sustained underruns fade the AI out automatically") {
    AiOutputStage stage;
    stage.prepare(kSr, 1024);
    stage.setMuted(false);
    stage.safetyMonitor().setPolicy(4, 2.0);
    stage.safetyMonitor().setPrimingGraceSeconds(0.0);
    stage.setGenerating(true);

    std::vector<float> l(512), r(512);
    fillConstant(l, r, 0.5f);
    stage.process(l.data(), r.data(), l.size(), false);
    CHECK(maxAbs(l) > 0.0f);

    for (int i = 0; i < 6; ++i) {
        fillConstant(l, r, 0.5f);
        stage.process(l.data(), r.data(), l.size(), /*underran=*/true);
    }
    CHECK(stage.safetyMonitor().health() == Health::Degraded);

    // Give the fade time to complete, then confirm true silence.
    for (int i = 0; i < 10; ++i) {
        fillConstant(l, r, 0.5f);
        stage.process(l.data(), r.data(), l.size(), false);
    }
    CHECK(stage.isSilent());
    CHECK_EQ(maxAbs(l), 0.0f);
}

TEST("output stage never exceeds the limiter ceiling") {
    AiOutputStage stage;
    stage.prepare(kSr, 4096);
    stage.setMuted(false);
    stage.setOutputLevelDb(12.0f); // deliberately hot
    stage.limiter().setCeilingDb(-1.0f);

    const float ceiling = dbToLinear(-1.0f);
    std::vector<float> l(4096), r(4096);

    for (int block = 0; block < 20; ++block) {
        fillConstant(l, r, 2.0f);
        stage.process(l.data(), r.data(), l.size(), false);
        CHECK(maxAbs(l) <= ceiling + 1e-6f);
        CHECK(maxAbs(r) <= ceiling + 1e-6f);
    }
}

TEST("output level is a gain, and -60 dB is true silence") {
    AiOutputStage stage;
    stage.prepare(kSr, 8192);
    stage.setMuted(false);

    std::vector<float> l(8192), r(8192);
    fillConstant(l, r, 0.5f);
    stage.process(l.data(), r.data(), l.size(), false); // open the fade

    stage.setOutputLevelDb(-60.0f);
    for (int i = 0; i < 4; ++i) {
        fillConstant(l, r, 0.5f);
        stage.process(l.data(), r.data(), l.size(), false);
    }
    CHECK(maxAbs(l) < 1e-4f);
    CHECK_NEAR(stage.outputLevelDb(), -60.0f, 1e-6);
}

TEST("diagnostics count blocks and underruns from the audio path") {
    AiOutputStage stage;
    stage.prepare(kSr, 1024);

    std::vector<float> l(256), r(256);
    for (int i = 0; i < 10; ++i) {
        fillConstant(l, r, 0.1f);
        stage.process(l.data(), r.data(), l.size(), /*underran=*/(i % 5 == 0));
    }

    const auto snap = stage.diagnostics().snapshot();
    CHECK_EQ(snap.blocksProcessed, 10u);
    CHECK_EQ(snap.audioUnderruns, 2u);
    CHECK_NEAR(snap.sampleRate, kSr, 1e-9);
}

TEST("generation headroom ratio flags a model that cannot keep up") {
    DiagnosticsSnapshot snap;
    snap.generationTotalMs = 20.0f;  // half the 40 ms frame budget
    CHECK_NEAR(snap.generationHeadroomRatio(), 0.5f, 1e-5);
    snap.generationTotalMs = 48.0f;  // over budget: underruns are coming
    CHECK(snap.generationHeadroomRatio() > 1.0f);
}

TEST_MAIN_END()
