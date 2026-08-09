// GhostBand — live AI accompaniment for singer-songwriters.
// Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.
//
// The AI Intensity macro. MRT2 has no density parameter, so this mapping is entirely
// ours — a hypothesis about which controls read as "the band is doing less". These tests
// cannot tell us whether it *sounds* right; only ears can. What they can guarantee is
// that it is monotonic, bounded, stable, and never touches the two things it must not.

#include "TestMain.h"
#include "backend/NullBackend.h"
#include "core/IntensityMacro.h"

using namespace ghostband::core;
using ghostband::backend::NullBackend;

TEST_MAIN_BEGIN("Intensity")

TEST("intensity is clamped to 0..1") {
    IntensityMacro macro;
    macro.setIntensity(-5.0f);
    CHECK_NEAR(macro.intensity(), 0.0f, 1e-6);
    macro.setIntensity(37.0f);
    CHECK_NEAR(macro.intensity(), 1.0f, 1e-6);
    macro.setIntensity(0.68f);
    CHECK_EQ(macro.percent(), 68);
}

TEST("every continuous parameter is monotonic in intensity") {
    // The single most important property. A knob that does not move consistently in one
    // direction is unusable live — the performer cannot build an intuition for it.
    IntensityMacro macro;

    float prev_drums = -1e9f, prev_coca = -1e9f, prev_temp = -1e9f;
    for (int i = 0; i <= 100; ++i) {
        macro.setIntensity(static_cast<float>(i) / 100.0f);
        const auto p = macro.compute();
        CHECK(p.cfgDrums >= prev_drums);
        CHECK(p.cfgMusicCoca >= prev_coca);
        CHECK(p.temperature >= prev_temp);
        prev_drums = p.cfgDrums;
        prev_coca = p.cfgMusicCoca;
        prev_temp = p.temperature;
    }
}

TEST("prompt weights are monotonic and always sum to 1") {
    // Summing to 1 keeps the conditioning *strength* constant so only its character
    // moves. A blend summing to less would quietly weaken the style as well as thin it.
    IntensityMacro macro;

    float prev_sparse = 2.0f, prev_full = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        macro.setIntensity(static_cast<float>(i) / 100.0f);
        const auto p = macro.compute();

        const float sum = p.promptWeights[0] + p.promptWeights[1] + p.promptWeights[2];
        CHECK_NEAR(sum, 1.0f, 1e-5);

        for (float w : p.promptWeights) {
            CHECK(w >= 0.0f);
            CHECK(w <= 1.0f);
        }

        CHECK(p.promptWeights[0] <= prev_sparse + 1e-6f); // sparse falls away
        CHECK(p.promptWeights[2] >= prev_full - 1e-6f);   // full comes in
        prev_sparse = p.promptWeights[0];
        prev_full = p.promptWeights[2];
    }
}

TEST("the extremes and the centre are what you would expect") {
    IntensityMacro macro;

    macro.setIntensity(0.0f);
    auto p = macro.compute();
    CHECK_NEAR(p.promptWeights[0], 1.0f, 1e-5);  // all sparse
    CHECK_NEAR(p.promptWeights[2], 0.0f, 1e-5);
    CHECK(p.drumless);

    macro.setIntensity(0.5f);
    p = macro.compute();
    CHECK_NEAR(p.promptWeights[1], 1.0f, 1e-5);  // the performer's own prompt, unmodified
    CHECK(!p.drumless);

    macro.setIntensity(1.0f);
    p = macro.compute();
    CHECK_NEAR(p.promptWeights[2], 1.0f, 1e-5);  // all full
    CHECK_NEAR(p.promptWeights[0], 0.0f, 1e-5);
    CHECK(!p.drumless);
}

TEST("drumless has hysteresis and does not flap") {
    // Without hysteresis a slow fade across the threshold drops the kit in and out
    // repeatedly, which is far more noticeable than the transition itself.
    IntensityMacro macro;

    macro.setIntensity(0.0f);
    CHECK(macro.compute().drumless);

    // Rising through the ON threshold does not immediately release it.
    macro.setIntensity(0.14f);
    CHECK(macro.compute().drumless);
    macro.setIntensity(0.16f);
    CHECK(macro.compute().drumless);
    macro.setIntensity(0.18f);
    CHECK(!macro.compute().drumless);

    // Falling back does not immediately re-engage it either.
    macro.setIntensity(0.16f);
    CHECK(!macro.compute().drumless);
    macro.setIntensity(0.14f);
    CHECK(!macro.compute().drumless);
    macro.setIntensity(0.12f);
    CHECK(macro.compute().drumless);
}

TEST("dithering around the threshold produces at most one transition") {
    IntensityMacro macro;
    macro.setIntensity(0.0f);
    CHECK(macro.compute().drumless);

    int transitions = 0;
    bool last = macro.compute().drumless;
    for (int i = 0; i < 50; ++i) {
        macro.setIntensity(i % 2 == 0 ? 0.145f : 0.155f); // straddling, inside the band
        const bool now = macro.compute().drumless;
        if (now != last) ++transitions;
        last = now;
    }
    CHECK_EQ(transitions, 0);
}

TEST("intensity never touches harmony tightness or output level") {
    // The two hard rules: cfg_notes governs how tightly the band follows the performer's
    // chords and must not loosen because the arrangement thinned; and AI Intensity is not
    // a volume control (brief §16).
    NullBackend backend;
    IntensityMacro macro;

    const float cfg_notes_before = backend.cfgNotes();

    for (float x : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        macro.setIntensity(x);
        macro.applyTo(backend);
        CHECK_NEAR(backend.cfgNotes(), cfg_notes_before, 1e-6);
    }
    // Nothing in the macro can reach a gain: applyTo takes only IGenerationBackend, whose
    // sole level control is setMute, and it is never called here.
    CHECK(!backend.muted());
}

TEST("applyTo pushes the computed values to the backend") {
    NullBackend backend;
    IntensityMacro macro;

    macro.setIntensity(1.0f);
    macro.applyTo(backend);
    const auto high = macro.compute();
    CHECK_NEAR(backend.cfgDrums(), high.cfgDrums, 1e-6);
    CHECK_NEAR(backend.cfgMusicCoca(), high.cfgMusicCoca, 1e-6);
    CHECK_NEAR(backend.temperature(), high.temperature, 1e-6);
    CHECK(!backend.drumless());

    macro.setIntensity(0.0f);
    macro.applyTo(backend);
    CHECK(backend.drumless());
    CHECK(backend.cfgDrums() < high.cfgDrums);
}

TEST("prompt variants are the performer's words plus density, in slot order") {
    IntensityMacro macro;
    const auto variants = macro.promptVariants("warm indie folk ensemble");

    CHECK_EQ(variants.size(), 3u);
    // Slot order must match promptWeights: [sparse, base, full].
    CHECK(variants[1] == "warm indie folk ensemble");
    CHECK(variants[0].find("warm indie folk ensemble") == 0);
    CHECK(variants[2].find("warm indie folk ensemble") == 0);
    CHECK(variants[0].find("sparse") != std::string::npos);
    CHECK(variants[2].find("full ensemble") != std::string::npos);
    // Never an artist reference (brief §8) — the suffixes describe musical attributes.
    CHECK(variants[0] != variants[2]);
}

TEST("the mapping is retunable without touching code") {
    // The whole point of the Tuning struct: this is a hypothesis nobody has heard yet, so
    // adjusting it must be data, not a rewrite.
    IntensityMacro macro;
    IntensityMacro::Tuning t;
    t.cfgDrumsMin = 0.0f;
    t.cfgDrumsMax = 8.0f;
    t.drumlessOnBelow = 0.5f;
    t.drumlessOffAbove = 0.6f;
    macro.setTuning(t);

    macro.setIntensity(1.0f);
    CHECK_NEAR(macro.compute().cfgDrums, 8.0f, 1e-5);
    macro.setIntensity(0.4f);
    CHECK(macro.compute().drumless);
}

TEST("all outputs stay inside sane bounds across the whole range") {
    // A macro that could hand MRT2 a wild guidance value would be a way to make the band
    // unusable from a single knob.
    IntensityMacro macro;
    for (int i = 0; i <= 100; ++i) {
        macro.setIntensity(static_cast<float>(i) / 100.0f);
        const auto p = macro.compute();
        CHECK(p.cfgDrums >= 0.0f && p.cfgDrums <= 8.0f);
        CHECK(p.cfgMusicCoca >= 0.0f && p.cfgMusicCoca <= 8.0f);
        CHECK(p.temperature > 0.0f && p.temperature <= 2.0f);
    }
}

TEST_MAIN_END()
